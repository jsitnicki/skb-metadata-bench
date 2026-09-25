// SPDX-License-Identifier: GPL-2.0
//
// Baseline packet counter for the CPU-delta benchmark.
//
//   tc-ingress: count every packet.
//   tc-egress:  count every packet.
//
// Does nothing else: no parsing, no kfuncs, no state. This isolates
// the fixed cost of having tc-BPF programs attached and running, so
// the skb-ext and gated-tp variants can be compared against it.

#include "vmlinux.h"

#include <bpf/bpf_helpers.h>

#define TC_ACT_OK 0

enum stat_idx {
	STAT_INGRESS = 0,
	STAT_EGRESS,
	STAT_MAX,
};

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

SEC("tc/ingress")
int cnt_ingress(struct __sk_buff *ctx)
{
	bump(STAT_INGRESS);
	return TC_ACT_OK;
}

SEC("tc/egress")
int cnt_egress(struct __sk_buff *ctx)
{
	bump(STAT_EGRESS);
	return TC_ACT_OK;
}

char LICENSE[] SEC("license") = "GPL";
