// SPDX-License-Identifier: GPL-2.0
//
// Gated-tracepoint variant of the per-packet metadata benchmark.
//
//   tc-egress:   write metadata into rhash[skb], arm SKB_EXT_TRACE
//                (enables skb_free/skb_copy/skb_scrub tracepoints),
//                count packet.
//   tc-ingress:  read the metadata back, count packet.
//   tp/skb_free: reap the rhash entry when the skb is freed.
//   tp/skb_copy: copy the entry when the skb is copied.
//
// Hook order on loopback: egress (TX) fires before ingress (RX), so
// metadata flows egress -> ingress.
//
// Same stats layout as cnt.bpf.c (key 0 = ingress, key 1 = egress)
// so cpustats.py results are directly comparable.

#include "vmlinux.h"

#include <bpf/bpf_helpers.h>
#include <bpf/bpf_tracing.h>

#define TC_ACT_OK 0

extern int bpf_trace_skb(struct sk_buff *skb, u64 flags__k) __weak __ksym;

enum stat_idx {
	STAT_INGRESS = 0,
	STAT_EGRESS,
	STAT_FREE_REAPED,
	STAT_COPY,
	STAT_MAX,
};

/* Metadata payload carried per-packet, keyed by skb pointer. */
struct meta {
	__u64 ingress_ts;	/* bpf_ktime_get_ns() at ingress */
	__u32 pkt_len;		/* ctx->len at ingress */
	__u32 magic;		/* sanity token */
};

#define META_MAGIC 0x5ebee55e

/* rhash: key = skb pointer, value = metadata. Entries are reclaimed
 * by the tp/skb_free program. Same shape as the skb-ext variant so
 * the rhash cost is identical on both sides of the comparison.
 */
struct {
	__uint(type, BPF_MAP_TYPE_RHASH);
	__uint(map_flags, BPF_F_NO_PREALLOC);
	__uint(max_entries, 16384);
	__type(key, struct __sk_buff *);
	__type(value, struct meta);
} meta_map SEC(".maps");

/* Stats, read from userspace. Keys 0/1 match cnt.bpf.c. */
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
int gtp_write(struct __sk_buff *ctx)
{
	struct meta init;

	bump(STAT_EGRESS);

	init.ingress_ts = bpf_ktime_get_ns();
	init.pkt_len = ctx->len;
	init.magic = META_MAGIC;

	/* BPF_ANY: tolerate re-tag if an skb ever passes twice. */
	if (bpf_map_update_elem(&meta_map, &ctx, &init, BPF_ANY))
		return TC_ACT_OK;

	/* Arm the SKB_EXT_TRACE bit: enables the gated skb_free /
	 * skb_copy / skb_scrub tracepoints for this skb.
	 */
	bpf_trace_skb(bpf_cast_to_kern_ctx(ctx), 0);

	return TC_ACT_OK;
}

SEC("tc/ingress")
int gtp_read(struct __sk_buff *ctx)
{
	struct meta *m;

	bump(STAT_INGRESS);

	m = bpf_map_lookup_elem(&meta_map, &ctx);
	if (!m || m->magic != META_MAGIC)
		return TC_ACT_OK;

	/* Round-trip verified; the entry itself is reaped by
	 * tp/skb_free, not here.
	 */
	return TC_ACT_OK;
}

/* Gated tracepoint: fires only for skbs with SKB_EXT_TRACE armed.
 * tp_btf context: args[0] = skb, args[1] = location.
 */
SEC("tp_btf/skb_free")
int BPF_PROG(gtp_skb_free, struct sk_buff *skb, void *location)
{
	if (!bpf_map_delete_elem(&meta_map, &skb))
		bump(STAT_FREE_REAPED);
	return 0;
}

/* Same gating; copies the entry to the new skb. */
SEC("tp_btf/skb_copy")
int BPF_PROG(gtp_skb_copy, struct sk_buff *dst, const struct sk_buff *src)
{
	struct meta *m;

	m = bpf_map_lookup_elem(&meta_map, &src);
	if (m)
		bpf_map_update_elem(&meta_map, &dst, m, BPF_NOEXIST);
	bump(STAT_COPY);
	return 0;
}

char LICENSE[] SEC("license") = "GPL";
