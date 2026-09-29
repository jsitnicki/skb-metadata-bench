# Benchmark report: BPF per-packet metadata — skb ext vs gated tracepoint

CPU overhead of two per-packet BPF metadata designs, measured on
loopback UDP at ~146k pps, comparing each against a tc counting-prog
floor on the same kernel.

Date: 2026-09-25. Both trees on v7.3-rc4 (`93f51579e7df`):

- gtp: `~/src/linux` (gated-tracepoints series, `CONFIG_SKB_GATED_TRACEPOINTS`)
- ext: `~/src/linux-skb-ext` (bpf-skb-ext series, `CONFIG_BPF_SKB_EXT`)

Measurement environment: 8-vCPU virtme-ng guest on i7-13800H, vCPUs
pinned 1:1 to host E-cores (12-19) clamped at 2700 MHz,
**KVM halt-polling disabled** (`halt_poll_ns=0` — see README; with
polling on, idle-heavier runs were systematically under-accounted and
ext appeared *cheaper than its own floor*, an artifact).

## Summary

Mean of 3 independent repetitions; busy in % of one core; Δ busy
(mean across the 6 measured CPUs) against the same tree's cnt floor,
± spread of the set deltas. ns/pkt charges the *aggregate* added CPU
(Δ × 6 cores) to the total packet rate (146k pps) — i.e. the full
TX+RX cost per packet:

| variant                        | Δ busy (mean/6)    | ns/pkt @146k |
|--------------------------------|--------------------|--------------|
| gated tracepoints (rhash)      | **+16.4 ± 2.8 pp** | **≈ 6750**   |
| gated tracepoints (percpu LRU) | **+10.0 ± 3.3 pp** | **≈ 4100**   |
| bpf skb ext                    | **+6.6 ± 1.1 pp**  | **≈ 2720**   |

**The skb extension is ≈ 2.5x cheaper per packet** than the gated
skb tracepoint design with rhash, ≈ 1.5x cheaper than the
per-CPU-LRU hash variant. Details below.

## Setup

- 3 iperf pairs, 36-byte datagrams, 14 mbit/s each; servers on guest
  CPUs 0/2/4, clients on 1/3/5; mpstat on CPU 6 sampling CPUs 0-5,
  60 x 1 s window
- variants: cnt (tc counter floor), gtp (rhash[skb] + `bpf_trace_skb`
  + `raw_tp/skb_free` reaper + `tp_btf/skb_copy`), gtplru (same as
  gtp but `BPF_MAP_TYPE_LRU_PERCPU_HASH` instead of rhash), ext
  (`bpf_dynptr_from_skb_ext` create at egress, find at ingress)
- every run validated: lo packet counts ~9.5M in 60 s, 0 errors/drops;
  BPF stats counters consistent (ext: FOUND == INGRESS == EGRESS);
  per-second busy series flat, per-CPU profile balanced
  (servers ~13-21%, clients ~46-83%)

## Results

Three independent repetitions (set = one floor + one test run per
tree). Mean busy % of one core across CPUs 0-5; deltas against the
same tree's floor; ns/pkt = (Δbusy × 6) / 100 / 146000 x 1e9
(aggregate added CPU over all 6 cores charged to total pps).

|                     | set 1      | set 2      | set 3      | mean            |
|---------------------|------------|------------|------------|-----------------|
| gtp-cnt floor          | 33.06      | 31.35      | 31.21      | 31.87           |
| gtp-gtp                | 46.20      | 49.48      | 49.19      | 48.29           |
| **gtp Δ busy (pp)**    | **+13.14** | **+18.13** | **+17.98** | **+16.4 ± 2.8** |
| **gtp ns/pkt**         | 5410       | 7450       | 7390       | **≈ 6750**      |
| gtp-gtplru             | 41.41      | 45.16      | 38.99      | 41.85           |
| **gtplru Δ busy (pp)** | **+8.34**  | **+13.81** | **+7.78**  | **+10.0 ± 3.3** |
| **gtplru ns/pkt**      | 3430       | 5680       | 3200       | **≈ 4100**      |
| ext-cnt floor          | 29.88      | 31.43      | 30.43      | 30.58           |
| ext-ext                | 37.20      | 36.73      | 37.55      | 37.16           |
| **ext Δ busy (pp)**    | **+7.33**  | **+5.30**  | **+7.12**  | **+6.6 ± 1.1**  |
| **ext ns/pkt**         | 3010       | 2180       | 2930       | **≈ 2720**      |

Errors on the means are spread-based (n=3). Cross-check without
floors: all six cnt runs sit at 29.9-33.1 (mean 31.2), and the direct
gtp-gtp − ext-ext gap (48.3 − 37.2 = +11.1 pp) matches the
delta-of-deltas (16.4 − 6.6 = +9.9 pp) within noise.

Note: the gtplru runs were taken 2026-09-29 in a later session than
their floors (2026-09-25); comparability rests on the floors agreeing
within noise across sessions (29.9-33.1 across all nine cnt runs).
All gtplru runs validated: FREE_REAPED == EGRESS (no hash leaks),
flat per-second busy series (rdev 4.2-6.2%).

### Where the cost sits (busy split, pp vs floor)

|       | sys          | soft         | note                                                                                                                                               |
|-------|--------------|--------------|----------------------------------------------------------------------------------------------------------------------------------------------------|
| gtp   | +5.3 .. +8.1 | +7.7 .. +9.7 | soft-dominated: `raw_tp/skb_free` reaper runs on the RX free path (`tp_btf/skb_copy` is attached but not exercised — no cloning on plain loopback) |
| gtplru| +3.9 .. +7.5 | +3.5 .. +5.8 | more balanced: cheaper map ops cut both the egress write (sys) and the `skb_free` reaper (soft)                                                  |
| ext   | +2.8 .. +4.0 | +2.3 .. +2.8 | sys-dominated: `skb_ext_add` alloc at tc egress (TX), read at ingress is cheap                                                                     |

## Conclusion

The BPF skb extension is **≈ 2.5x cheaper per packet** than the gated
skb tracepoint design (rhash) for carrying per-packet metadata
through the stack:

- **ext: ≈ 2720 ns/pkt** (2180-3010 across sets)
- **gtplru: ≈ 4100 ns/pkt** (3200-5680 across sets)
- **gtp: ≈ 6750 ns/pkt** (5410-7450 across sets)

Swapping the rhash for a per-CPU LRU hash recovers ~40% of the gated
tracepoint overhead (6750 -> 4100 ns/pkt) — the rhash's spinlock and
refcount traffic is a large part of the cost — but ext remains
≈ 1.5x cheaper than the best map variant. The lowest gtp estimate
(5410) is still ~1.8x the highest ext one (3010), so the ext < gtp
ordering is robust despite gtp's wider set-to-set scatter.

Note these ns/pkt figures are *total system cost per packet* (TX hook
+ RX hook + knock-on pipeline effects), not the raw cost of the
metadata mechanism in isolation — e.g. 2.7 µs/pkt for ext is far more
than the `skb_ext_add` alloc/free + 16-byte copy itself costs. The
numbers measure end-to-end CPU attribution under this traffic
pattern; ratios between variants are the robust signal.

The qualitative reason is visible in the busy split: the
gated-tracepoint design pays most of its overhead in softirq (reaping
rhash entries at `skb_free`), while the extension rides on the skb —
no reaper, no hash table; its dominant cost is the chunk allocation
at TX.

## Raw data

All three sets are in-tree: `results-1/`, `results-2/`, `results-3/`
(rows "set 1/2/3" above, respectively). Each holds
`<tree>-<obj>.json` (mpstat) + `<tree>-<obj>.log` (uname, lo
counters, BPF stats map) for the runs of the set, including the
three gtplru runs (`gtp-gtplru.*`). Reproduce a
set's tables with:

```sh
python3 tools/cpustats.py \
    results-1/gtp-cnt.json results-1/gtp-gtp.json \
    results-1/ext-cnt.json results-1/ext-ext.json
```
