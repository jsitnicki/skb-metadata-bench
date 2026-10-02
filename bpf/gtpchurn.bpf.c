// SPDX-License-Identifier: GPL-2.0
//
// Gated-tracepoint variant WITHOUT map_extra/nelem_hint.
//
// Identical datapath to gtp.bpf.c, but the rhash is created with
// map_extra = 0: no nelem_hint and (with the min_size = map_extra
// kernel patch) no min_size floor either. automatic_shrinking is
// still on, min_size bottoms out at HASH_MIN_SIZE = 4, so at the
// ~7-16-entry live set the table rides both the 30% shrink and 75%
// grow watermarks on every packet. This is the resize-churn demo:
// constant rehash windows, insert-side -EBUSY, irq_work storms.
//
// Compare against gtp.bpf.o (map_extra = 256) to quantify the cost
// of churn, and against gtplru.bpf.o (fixed-size, no resize).

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
	STAT_UPDATE_ERROR,
	STAT_MAX,
};

/* Metadata payload carried per-packet, keyed by skb pointer. */
struct meta {
	__u64 ingress_ts;	/* bpf_ktime_get_ns() at ingress */
	__u32 pkt_len;		/* ctx->len at ingress */
	__u32 magic;		/* sanity token */
};

#define META_MAGIC 0x5ebee55e

/* rhash with NO map_extra: nelem_hint = 0 -> default initial size,
 * min_size -> HASH_MIN_SIZE = 4. automatic_shrinking = true (BPF
 * hard-sets it) means the table continuously shrinks toward the tiny
 * live set and grows back, exercising the rehash slow path on a large
 * fraction of inserts.
 */
struct {
	__uint(type, BPF_MAP_TYPE_RHASH);
	__uint(map_flags, BPF_F_NO_PREALLOC);
	__uint(max_entries, 1048576);
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
	int ret;

	bump(STAT_EGRESS);

	init.ingress_ts = bpf_ktime_get_ns();
	init.pkt_len = ctx->len;
	init.magic = META_MAGIC;

	/* Arm the SKB_EXT_TRACE bit: enables the gated skb_free /
	 * skb_copy / skb_scrub tracepoints for this skb.
	 */
	bpf_trace_skb(bpf_cast_to_kern_ctx(ctx), 0);

	/* BPF_ANY: tolerate re-tag if an skb ever passes twice. */
	ret = bpf_map_update_elem(&meta_map, &ctx, &init, BPF_ANY);
	if (ret)
		bump(STAT_UPDATE_ERROR);

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
