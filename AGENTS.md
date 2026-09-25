# Agent notes — loopback metadata benchmark

Operational details an agent needs to work in this directory without
re-deriving them. Methodology lives in [README.md](README.md).

## Environment

- Two kernel trees: gtp = `~/src/linux` (gated skb tracepoints,
  `CONFIG_SKB_GATED_TRACEPOINTS`), ext = `~/src/linux-skb-ext` (BPF skb
  extension, `CONFIG_BPF_SKB_EXT`). Set in [Justfile](Justfile).
- Benchmarks run inside a virtme-ng VM (8 vCPUs) booted per run by
  [run.sh](run.sh); `--rwdir results/` writes the mpstat JSON straight
  into the host `results/`.
- [bench.sh](bench.sh) uses `iperf` (not iperf3), 36-byte UDP
  datagrams, 14 mbit/s × 3 pairs ≈ 146k pps; CPU layout: servers 0/2/4,
  clients 1/3/5, mpstat on 6.

## Tools

- [tools/cpustats.py](tools/cpustats.py) — aggregates mpstat JSON.
  Four-file mode (floor1 test1 floor2 test2) prints two markdown
  tables: results (busy/sys/soft, rdev) and deltas
  (test − floor, ±1σ propagated in quadrature from per-run rdevs, plus
  ns/pkt at `--pps`, default 146k). `--no-md` for plain output only.
  Not executable — invoke with `python3 tools/cpustats.py ...`.
- [tools/show_stats.py](tools/show_stats.py) — dumps the BPF `stats`
  map via bpftool (INGRESS/EGRESS/FOUND/... counters) from inside the
  VM; used to confirm programs actually ran and metadata flowed.
- [tools/pin-vcpus-threads.py](tools/pin-vcpus-threads.py) — pins vCPU
  threads via QMP; `just pin`, auto-invoked by run.sh.

## Result files

- `results/<tree>-<obj>.json`: `<tree>` is the kernel tree (gtp/ext),
  `<obj>` the attached BPF object (cnt/gtp/ext). Naming is positional —
  cpustats expects floor before test per tree.

## Validation

- Watch for bimodal runs before accepting numbers: busy dips well below
  the floor mean for a stretch, rdev jumps to ~10%+ (seen 2026-09-24 on
  ext-ext; re-run fixed it). Quick check: per-run min/p10 vs mean from
  cpustats output.
- Expected sane ranges (2026-09-24/25): floors ~43-46 busy, rdev ≤ 8%;
  deltas gtp ≈ +5-6, ext ≈ +8-11, sys/soft split roughly even.

## Pitfalls

- vng VM clock lags the host (~21 h): after host-side source edits,
  `rm` the affected `.o` before `vng -- make` or the rebuild silently
  no-ops (stale vmlinux/BTF). Verify kfuncs with `nm vmlinux | grep`.
- Deltas are only meaningful against the same tree's floor — cross-tree
  absolute comparisons carry a ~2-3 pp floor offset.
- Run-to-run floor drift of a few pp happens between VM boots; compare
  only runs from the same session's set of four.
