// SPDX-License-Identifier: GPL-2.0
//
// Minimal loader: load a BPF object, tc-attach its tc/* programs to
// an interface (clsact ingress/egress), and auto-attach its tracing
// programs (tp_btf, raw_tp) via libbpf links.
//
// Works for all benchmark variants (cnt, gtp, ext): only
// the programs present in the object get attached.
//
// Usage: attach <obj.o> [iface]    - load + attach, pin links, exit
//        attach -d [iface]         - detach tc, destroy pinned links
//
// Links survive process exit because they are pinned under
// /sys/fs/bpf/bench-links. Detach removes them.

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include <bpf/libbpf.h>
#include <bpf/bpf.h>
#include <net/if.h>

#define PIN_DIR "/sys/fs/bpf/bench-links"

static int libbpf_print(enum libbpf_print_level level, const char *fmt,
			va_list args)
{
	if (level == LIBBPF_DEBUG)
		return 0;
	return vfprintf(stderr, fmt, args);
}

/* tc-attach one program at one hook via a bpf link (clsact must
 * already exist; we create it first). */
static int tc_attach(int ifindex, const char *iface,
		     enum bpf_tc_attach_point ap, struct bpf_program *prog,
		     const char *what)
{
	DECLARE_LIBBPF_OPTS(bpf_tc_hook, hook,
		.ifindex = ifindex, .attach_point = ap);
	DECLARE_LIBBPF_OPTS(bpf_tc_opts, opts,
		.prog_fd = bpf_program__fd(prog));
	int err;

	err = bpf_tc_attach(&hook, &opts);
	if (err) {
		fprintf(stderr, "tc attach %s on %s: %s\n",
			what, iface, strerror(-err));
		return err;
	}
	printf("tc %s attached on %s\n", what, iface);
	return 0;
}

static int do_attach(const char *objpath, const char *iface)
{
	struct bpf_program *prog;
	struct bpf_object *obj;
	struct bpf_link *link;
	int ifindex, err;
	char pin[256];

	ifindex = if_nametoindex(iface);
	if (!ifindex) {
		fprintf(stderr, "no such interface: %s\n", iface);
		return 1;
	}

	mkdir(PIN_DIR, 0755); /* ok if exists */

	obj = bpf_object__open_file(objpath, NULL);
	if (libbpf_get_error(obj)) {
		fprintf(stderr, "open %s failed\n", objpath);
		return 1;
	}
	err = bpf_object__load(obj);
	if (err) {
		fprintf(stderr, "load %s: %s\n", objpath, strerror(-err));
		return 1;
	}

	/* clsact qdisc needed for both hooks */
	{
		DECLARE_LIBBPF_OPTS(bpf_tc_hook, qhook,
			.ifindex = ifindex,
			.attach_point = BPF_TC_INGRESS | BPF_TC_EGRESS);
		bpf_tc_hook_create(&qhook); /* EEXIST is fine */
	}

	bpf_object__for_each_program(prog, obj) {
		const char *sec = bpf_program__section_name(prog);
		const char *name = bpf_program__name(prog);

		if (!strncmp(sec, "tc/ingress", 10)) {
			tc_attach(ifindex, iface, BPF_TC_INGRESS, prog, "ingress");
		} else if (!strncmp(sec, "tc/egress", 9)) {
			tc_attach(ifindex, iface, BPF_TC_EGRESS, prog, "egress");
		} else {
			/* tracing progs (tp_btf, raw_tp): auto-attach */
			link = bpf_program__attach(prog);
			if (libbpf_get_error(link)) {
				fprintf(stderr, "attach %s (%s) failed\n",
					name, sec);
				continue;
			}
			snprintf(pin, sizeof(pin), "%s/%s", PIN_DIR, name);
			err = bpf_link__pin(link, pin);
			if (err)
				fprintf(stderr, "pin %s: %s\n",
					name, strerror(-err));
			else
				printf("%s attached + pinned\n", name);
		}
	}
	return 0;
}

static int do_detach(const char *iface)
{
	int ifindex = if_nametoindex(iface);

	if (ifindex) {
		DECLARE_LIBBPF_OPTS(bpf_tc_hook, hook,
			.ifindex = ifindex,
			.attach_point = BPF_TC_INGRESS | BPF_TC_EGRESS);
		bpf_tc_hook_destroy(&hook);
		printf("tc detached from %s\n", iface);
	}
	/* destroy pinned tracing links */
	{
		char cmd[300];
		snprintf(cmd, sizeof(cmd), "rm -f %s/* 2>/dev/null", PIN_DIR);
		if (system(cmd) == 0)
			printf("tracing links removed\n");
	}
	return 0;
}

int main(int argc, char **argv)
{
	libbpf_set_print(libbpf_print);

	if (argc >= 2 && !strcmp(argv[1], "-d"))
		return do_detach(argc >= 3 ? argv[2] : "lo");
	if (argc < 2) {
		fprintf(stderr, "usage: %s <obj.o> [iface] | -d [iface]\n",
			argv[0]);
		return 1;
	}
	return do_attach(argv[1], argc >= 3 ? argv[2] : "lo");
}
