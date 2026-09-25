# Loopback metadata benchmark

CPU overhead of per-packet BPF metadata implementations, measured on
loopback UDP traffic. Compared variants:

- **cnt** — tc ingress/egress programs that only bump per-CPU counters
  (the floor), `bpf/cnt.bpf.c`
- **gtp** — gated skb tracepoints: rhash[skb] write at tc egress +
  `bpf_trace_skb()` arming, read at tc ingress, `raw_tp/skb_free` +
  `tp_btf/skb_copy` for lifecycle, `bpf/gtp.bpf.c`
- **ext** — BPF skb extension: `bpf_dynptr_from_skb_ext()` write at tc
  egress, read-only find at tc ingress, `bpf/ext.bpf.c`

## Setup

### Kernel trees

Two kernel trees, one per metadata mechanism (see [Justfile](Justfile)):

- gtp: `~/src/linux` — gated skb tracepoints (`CONFIG_SKB_GATED_TRACEPOINTS`)
- ext: `~/src/linux-skb-ext` — BPF skb extension (`CONFIG_BPF_SKB_EXT`)

Each tree also runs the cnt variant as its own floor; deltas are only
ever computed against the same tree's floor.

### BPF programs

Each variant ships one object with a tc-ingress and a tc-egress program
([bpf/](bpf/)), attached to loopback by [attach.sh](attach.sh)
(`tc qdisc replace dev lo clsact` + filters on both hooks):

- cnt: bumps a per-CPU array counter per packet, nothing else — the
  fixed cost of having tc-BPF attached
- gtp: tc egress writes into an rhashtable keyed by `&skb` and arms the
  skb with `bpf_trace_skb()`; tc ingress reads the entry back; a
  `raw_tp/skb_free` program reaps entries at free time and a
  `tp_btf/skb_copy` program propagates them across clones
- ext: tc egress allocates/writes the BPF skb extension via
  `bpf_dynptr_from_skb_ext(F_CREATE)`; tc ingress reads it back
  read-only

`make` builds the objects and `attach` from [attach.c](attach.c)
(libbpf loader: tc-attach + tracing auto-attach for any variant),
called by [attach-and-bench.sh](attach-and-bench.sh).

### Traffic and pinning

Inside an 8-vCPU virtme-ng guest ([bench.sh](bench.sh)):

- 3 iperf UDP client/server pairs over loopback, 36-byte datagrams,
  14 mbit/s per pair ≈ 146k pps aggregate
- servers pinned to CPUs 0/2/4, clients to 1/3/5
- mpstat pinned to CPU 6, measuring CPUs 0-5: `mpstat -o JSON -P 0-5`
- timing: servers start 5 s before clients, clients 5 s before mpstat;
  mpstat window is 60 s (1 s samples), clients run 70 s
- on the host, vCPU threads are pinned to dedicated host threads via
  QMP once the VM is up (`just pin`,
  [tools/pin-vcpus-threads.py](tools/pin-vcpus-threads.py))

### Run flow

[run.sh](run.sh) drives one run from the host: boots the VM with
`just vm-<tree>` (8 vCPUs, `--rwdir results/`, QMP socket), pins vCPUs
in the background, then inside the guest runs
[attach-and-bench.sh](attach-and-bench.sh) — attach the BPF object,
run the benchmark, copy the mpstat JSON out to
`results/<tree>-<obj>.json` on the host.

## Running

One run per VM invocation, driven from the host:

```sh
./run.sh gtp cnt      # floor on the gtp tree  -> results/gtp-cnt.json
./run.sh gtp gtp      # test on the gtp tree  -> results/gtp-gtp.json
./run.sh ext  cnt     # floor on the ext tree  -> results/ext-cnt.json
./run.sh ext  ext     # test on the ext tree  -> results/ext-ext.json
```

`run.sh` boots the VM (`just vm-<tree>`), pins vCPU threads once QMP is
up, attaches the BPF object and runs `bench.sh` inside the guest; the
mpstat JSON is copied back into `results/`.

## Crunching

[tools/cpustats.py](tools/cpustats.py) aggregates the mpstat JSON. Pass
the four runs as `floor1 test1 floor2 test2` to get the markdown tables:

```sh
python3 tools/cpustats.py \
    results/gtp-cnt.json results/gtp-gtp.json \
    results/ext-cnt.json results/ext-ext.json
```

Output: the plain per-type table, then two markdown tables —

- results: mean busy/sys/soft (% of one core) with per-run rdev
- deltas (test − floor): ±1 stddev, propagated in quadrature from the
  per-run rdevs, plus a ns/pkt row derived from the busy delta at
  `--pps` (default 146k pps)

## Caveats

- Per-run rdev is within-run temporal spread (per-second samples), not
  sampling noise; longer windows don't shrink it much. To tighten the
  deltas, repeat run pairs and average the deltas.
- Watch for bimodal runs (traffic stalls): a run whose busy time dips
  below the floor's mean for a stretch (elevated rdev, ~10%+) is junk —
  re-run it.
- Only same-tree deltas (test − floor) are meaningful; cross-kernel
  absolute comparisons carry a ~2-3 pp floor offset that dominates.
