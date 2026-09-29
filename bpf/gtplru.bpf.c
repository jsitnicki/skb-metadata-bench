// SPDX-License-Identifier: GPL-2.0
//
// Gated-tracepoint variant with a per-cpu LRU hash instead of RHASH.
//
// Purpose: isolate the map cost from the gated-tracepoint cost.
// gtp.bpf.c measured ~2.5x more overhead than the ext variant; this
// variant keeps everything identical (same hooks, same stats, same
// bpf_trace_skb arming) and only swaps BPF_MAP_TYPE_RHASH for
// BPF_MAP_TYPE_LRU_PERCPU_HASH. If the delta vs the floor collapses,
// the overhead lives in the RHASH (bpf_mem_alloc + bucket lock);
// if it persists, it is the gated tracepoints / extra programs.
//
// Semantics differ from RHASH in one important way: updates,
// lookups and deletes are CPU-local. On loopback egress->ingress
// and the skb free all normally run on the transmit CPU, so the
// common path behaves like gtp. But a free that lands on another
// CPU misses the delete -- the entry then lingers until LRU
// eviction. max_entries is PER-CPU, so 8 CPUs x 16384 entries
// absorb the leak easily at benchmark packet rates; treat
// STAT_FREE_REAPED as a lower bound, not an exact count.
//
//   tc-egress:   write metadata into lru-hash[skb], arm SKB_EXT_TRACE,
//                count packet.
//   tc-ingress:  read the metadata back, count packet.
//   tp/skb_free: reap the entry when the skb is freed (same-CPU only).
//   tp/skb_copy: copy the entry when the skb is copied.
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

/* Per-cpu LRU hash: key = skb pointer, value = metadata.
 *
 * No BPF_F_NO_PREALLOC: LRU maps manage their own storage and the
 * flag is only meaningful for plain hash. Entries are reclaimed by
 * the tp/skb_free program when the free runs on the writing CPU,
 * otherwise by LRU eviction.
 */
struct {
	__uint(type, BPF_MAP_TYPE_LRU_PERCPU_HASH);
	__uint(max_entries, 16384);	/* per CPU */
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
int gtplru_write(struct __sk_buff *ctx)
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
int gtplru_read(struct __sk_buff *ctx)
{
	struct meta *m;

	bump(STAT_INGRESS);

	m = bpf_map_lookup_elem(&meta_map, &ctx);
	if (!m || m->magic != META_MAGIC)
		return TC_ACT_OK;

	/* Round-trip verified; the entry itself is reaped by
	 * tp/skb_free (same-CPU) or LRU eviction, not here.
	 */
	return TC_ACT_OK;
}

/* Gated tracepoint: fires only for skbs with SKB_EXT_TRACE armed.
 * tp_btf context: args[0] = skb, args[1] = location.
 *
 * Delete is CPU-local: a free on a foreign CPU misses and the entry
 * falls out via LRU eviction instead, so FREE_REAPED undercounts.
 */
SEC("tp_btf/skb_free")
int BPF_PROG(gtplru_skb_free, struct sk_buff *skb, void *location)
{
	if (!bpf_map_delete_elem(&meta_map, &skb))
		bump(STAT_FREE_REAPED);
	return 0;
}

/* Same gating; copies the entry to the new skb. Copy happens on the
 * CPU doing the copying, which owns src's entry in the common case.
 */
SEC("tp_btf/skb_copy")
int BPF_PROG(gtplru_skb_copy, struct sk_buff *dst, const struct sk_buff *src)
{
	struct meta *m;

	m = bpf_map_lookup_elem(&meta_map, &src);
	if (m)
		bpf_map_update_elem(&meta_map, &dst, m, BPF_NOEXIST);
	bump(STAT_COPY);
	return 0;
}

char LICENSE[] SEC("license") = "GPL";
