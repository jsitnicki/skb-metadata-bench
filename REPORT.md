# Benchmark report: BPF per-packet metadata — skb ext vs gated tracepoint

Direct per-program CPU cost of four per-packet BPF metadata designs,
measured with BPF prog stats (`kernel.bpf_stats_enabled`,
`run_time_ns` / `run_cnt`) on loopback UDP at ~568k pps.

Date: 2026-10-02. Both trees on v7.3-rc4. Kernel carries the
`min_size = map_extra` rhashtable floor patch
(`kernel/bpf/hashtab.c`), so the `map_extra = 256` rhash variant
keeps a 256-bucket floor instead of collapsing to `HASH_MIN_SIZE`.
All numbers are the mean ± spread (half range) of 3 independent
repetitions.

Measurement environment: 8-vCPU virtme-ng guest on i7-13800H, vCPUs
pinned 1:1 to host CPUs 12-19, KVM halt-polling disabled
(`halt_poll_ns=0`). BPF prog stats count each program's full
execution time while the static key is on — a direct measurement, no
mpstat-delta inference.

## Summary

Absolute per-packet BPF cost (sum of the variant's progs'
`run_time_ns / run_cnt`, mean ± spread over 3 sets):

| variant                              | BPF ns/pkt      |
|--------------------------------------|-----------------|
| **skb ext dynptr (ext)**             | **264 ± 2**     |
| **gtp (per-cpu LRU hash)**           | **543 ± 1**     |
| **gtp (rhash, min_size = 256)**      | **921 ± 35**    |
| **gtp (rhash, vanilla / unfloored)** | **1191 ± 44**   |

**The skb extension is the cheapest metadata carrier** — 2.1x cheaper
than the best map variant (per-cpu LRU), 3.5x cheaper than the floored
rhash, and 4.5x cheaper than the rhash without a size floor. The rhash
variants also scatter far more run-to-run (±35-44) than ext/lru (±1-2)
— resize machinery cost varies even when floored.

## Setup

- 3 iperf pairs, 36-byte datagrams, 50 mbit/s each (~568k pps total
  sustained, uniform run_cnt ~34.1M across all variants — no
  saturation); servers on guest CPUs 0/2/4, clients on 1/3/5, 60 s
  window.
- variants: **gtp (rht min_size)** — `BPF_MAP_TYPE_RHASH` keyed by
  `&skb`, `map_extra = 256` (nelem_hint +, with the kernel patch,
  `min_size` floor), `bpf_trace_skb` arming + `tp_btf/skb_free` reaper
  + `tp_btf/skb_copy`; **gtp (vanilla)** — same datapath, no
  `map_extra`, so `min_size` bottoms out at `HASH_MIN_SIZE = 4` and
  the autoshrinker rides the watermarks; **gtp (per-cpu lru)** — same
  but `BPF_MAP_TYPE_LRU_PERCPU_HASH` (fixed size, no resize);
  **ext** — `bpf_dynptr_from_skb_ext` create at egress, find at
  ingress, no map, no reaper.
- BPF stats: `echo 1 > /proc/sys/kernel/bpf_stats_enabled` for the
  whole run, then `bpftool -j prog show` at the end. Fresh VM per
  variant, so `run_time_ns`/`run_cnt` accumulate from zero. Per-prog
  ns/pkt = `run_time_ns / run_cnt`; variant total = sum of its progs.

## Results

Per-program ns/pkt (mean of 3 sets):

| prog                | gtp (min_size) | gtp (vanilla) | gtp (percpu LRU) | ext   |
|---------------------|----------------|---------------|------------------|-------|
| write (tc egress)   | 551            | 799           | 393              | 200   |
| read (tc ingress)   | 88             | 84            | 49               | 64    |
| skb_free (reaper)   | 281            | 307           | 100              | —     |
| **total**           | **921**        | **1191**      | **543**          | **264** |

`tp_btf/skb_copy` recorded zero runs on all gated variants — plain
loopback UDP does not clone skbs, so the copy path is attached but
never exercised.

## Notes on the rhash variants

The vanilla (unfloored) rhash pays **+270 ns/pkt (+29%) over the
floored one** — same datapath, same packet count, the only difference
is the table oscillating at the 30%/75% shrink/grow watermarks
instead of resting at 256 buckets. That is the resize-churn cost:
slow-path inserts through `rhashtable_insert_slow` →
`rhashtable_insert_rehash`, plus the irq_work → workqueue rehash
kicks on threshold crossings. It lands almost entirely in the write
path (799 vs 551 ns/pkt).

Both rhash variants also log a low rate of `bpf_map_update_elem`
errors: floored 64.5k-72.0k and vanilla 26.8k-40.4k across the 3 sets
(~34M packets each, i.e. 0.08-0.21%). On the vanilla table these are
the rehash-race `-EBUSY`; on the floored table they are `-EEXIST` on
re-tagged skb addresses (slab reuse). With the trace-first arm
ordering, a failed insert leaves no map entry and the reaper simply
finds nothing — no leak, occupancy stays low.

## Conclusion

For per-packet metadata on this workload, the skb extension dynptr is
the cheapest carrier by a wide margin: **264 ns/pkt**, vs **543** for
the best map-based design (per-cpu LRU) and **921** for the
rhashtable design even with a size floor. Removing the floor costs
another ~270 ns/pkt of pure resize churn (**1191**), which is what
the `min_size = map_extra` patch eliminates.

The qualitative split matches the mechanism: the gated-tracepoint
designs pay a reaper on the RX free path (281-307 ns/pkt for rhash,
100 for LRU) on top of a more expensive egress write, while the
extension rides on the skb — no reaper, no hash table — and its cost
concentrates in the egress `skb_ext` allocation (200 ns/pkt).

## Raw data

Three repetition sets: `results-1/`, `results-2/`, `results-3/`. Each
holds `<tree>-<variant>.bpfstats` (raw `bpftool -j prog show`) +
matching `.json` (mpstat, kept for cross-check) + `.log` (uname, lo
counters, stats map). Reproduce the summary with:

```sh
just bpfstats        # = tools/bpfstats.py --agg results-1 results-2 results-3
```
