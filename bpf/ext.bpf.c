// SPDX-License-Identifier: GPL-2.0
//
// BPF skb extension (dynptr) variant of the per-packet metadata
// benchmark. Requires the bpf-skb-ext-v2 kernel (CONFIG_BPF_SKB_EXT).
//
//   tc-egress:   create the skb ext dynptr, write metadata into it,
//                count packet.
//   tc-ingress:  find the ext dynptr (read-only), verify metadata,
//                count packet.
//
// Hook order on loopback: egress (TX) fires before ingress (RX), so
// metadata flows egress -> ingress. No rhash, no tracepoints, no
// reaping: the extension chunk rides on the skb and is freed with
// it. Same stats layout as cnt.bpf.c (key 0 = ingress,
// key 1 = egress) so results are comparable.

#include "vmlinux.h"

#include <bpf/bpf_helpers.h>

#define TC_ACT_OK 0

/* kfunc provided by the bpf-skb-ext-v2 kernel */
extern int bpf_dynptr_from_skb_ext(struct __sk_buff *skb, __u32 size,
				   __u64 flags__k,
				   struct bpf_dynptr *ptr__uninit) __ksym;

#ifndef BPF_SKB_EXT_F_CREATE
#define BPF_SKB_EXT_F_CREATE (1ULL << 0)
#endif

enum stat_idx {
	STAT_INGRESS = 0,	/* tc ingress hook ran */
	STAT_EGRESS,		/* tc egress hook ran */
	STAT_CREATE_FAIL,	/* dynptr F_CREATE failed at egress */
	STAT_FIND_FAIL,		/* dynptr find failed at ingress (ENOENT) */
	STAT_SLICE_FAIL,	/* dynptr_slice failed at ingress */
	STAT_BADMAGIC,		/* magic mismatch at ingress */
	STAT_FOUND,		/* metadata verified at ingress */
	STAT_MAX,
};

/* Metadata payload, carried inside the skb extension. Same shape as
 * the gated-tp variant's rhash value.
 */
struct meta {
	__u64 ingress_ts;	/* bpf_ktime_get_ns() at ingress */
	__u32 pkt_len;		/* ctx->len at ingress */
	__u32 magic;		/* sanity token */
};

#define META_MAGIC 0x5ebee55e

struct {
	__uint(type, BPF_MAP_TYPE_PERCPU_ARRAY);
	__uint(max_entries, STAT_MAX);
	__type(key, __u32);
	__type(value, __u64);
} stats SEC(".maps");

static __always_inline void bump(enum stat_idx i)
{
	__u32 key = i;
	__u64 *v = bpf_map_lookup_elem(&stats, &key);

	if (v)
		(*v)++;
}

SEC("tc/egress")
int ext_write(struct __sk_buff *ctx)
{
	struct bpf_dynptr ptr;
	struct meta m;

	bump(STAT_EGRESS);

	m.ingress_ts = bpf_ktime_get_ns();
	m.pkt_len = ctx->len;
	m.magic = META_MAGIC;

	if (bpf_dynptr_from_skb_ext(ctx, sizeof(m), BPF_SKB_EXT_F_CREATE,
				    &ptr)) {
		bump(STAT_CREATE_FAIL);
		return TC_ACT_OK;
	}
	bpf_dynptr_write(&ptr, 0, &m, sizeof(m), 0);
	return TC_ACT_OK;
}

SEC("tc/ingress")
int ext_read(struct __sk_buff *ctx)
{
	struct bpf_dynptr ptr;
	const struct meta *m;

	bump(STAT_INGRESS);

	if (bpf_dynptr_from_skb_ext(ctx, sizeof(*m), 0, &ptr)) {
		bump(STAT_FIND_FAIL);
		return TC_ACT_OK;
	}
	m = bpf_dynptr_slice(&ptr, 0, NULL, sizeof(*m));
	if (!m) {
		bump(STAT_SLICE_FAIL);
		return TC_ACT_OK;
	}
	if (m->magic != META_MAGIC) {
		bump(STAT_BADMAGIC);
		return TC_ACT_OK;
	}
	bump(STAT_FOUND);
	return TC_ACT_OK;
}

char LICENSE[] SEC("license") = "GPL";
