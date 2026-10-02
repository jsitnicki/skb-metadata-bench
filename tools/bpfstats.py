#!/usr/bin/env python3
"""Summarize per-program BPF run cost from a bench-bpfstats sidecar.

Reads one or more .bpfstats files (raw `bpftool -j prog show` output,
possibly preceded by `#` comment lines) and prints, per program:

    run_cnt, run_time_ns, and run_time_ns / run_cnt  (ns per invocation)

Sum the ns/invocation column across a variant's progs to get the total
per-packet BPF overhead for that variant. Programs with no recorded
runs (run_cnt == 0, e.g. gtp_skb_copy on a clone-free workload) are
listed but contribute 0.

Usage:
  bpfstats.py <file.bpfstats> [more.bpfstats ...]   per-file table
  bpfstats.py --agg <dir> [more dirs ...]           cross-set summary

--agg mode: each directory is one repetition set holding
<tree>-<variant>.bpfstats files. For every variant it prints each
prog's ns/pkt per set, then the variant TOTAL as mean ± spread.
ns/pkt is normalized to the set's busiest tc prog run_cnt, so sets of
different length/pps stay comparable.
"""

import json
import math
import os
import sys


def load_progs(path):
    with open(path) as f:
        # Strip leading '# ' comment lines; the rest is one JSON array.
        text = "".join(line for line in f if not line.startswith("#"))
    data = json.loads(text)
    if isinstance(data, dict):  # single prog object -> wrap
        data = [data]
    return data


def summarize(path):
    """Return (ref_cnt, {prog_name: ns_per_ref_pkt}) for one file.

    ns/run for a prog = run_time_ns / run_cnt. Per-packet cost is
    normalized to ref_cnt = max run_cnt among sched_cls (tc) progs,
    so progs that run once per packet (reaper included) are weighted
    by the packet count, not their own invocation count.
    """
    progs = load_progs(path)
    ref_cnt = max((p.get("run_cnt", 0) for p in progs
                   if p.get("type") == "sched_cls"), default=0)
    per = {}
    for p in progs:
        cnt = p.get("run_cnt", 0)
        ns = p.get("run_time_ns", 0)
        if cnt and ref_cnt:
            per[p.get("name", "?")] = ns / cnt * (cnt / ref_cnt)
    return ref_cnt, per


def show_file(path):
    progs = load_progs(path)
    print(f"== {path} ==")
    total_ns_per_run = 0.0
    rows = []
    for p in progs:
        cnt = p.get("run_cnt", 0)
        ns = p.get("run_time_ns", 0)
        per = (ns / cnt) if cnt else 0.0
        total_ns_per_run += per
        rows.append((p.get("name", "?"), p.get("type", "?"),
                     cnt, ns, per))
    name_w = max((len(r[0]) for r in rows), default=8)
    print(f"  {'prog':<{name_w}}  {'type':<10} {'run_cnt':>12} "
          f"{'run_time_ns':>16} {'ns/run':>9}")
    for name, ptype, cnt, ns, per in rows:
        print(f"  {name:<{name_w}}  {ptype:<10} {cnt:>12} "
              f"{ns:>16} {per:>9.2f}")
    print(f"  {'TOTAL':<{name_w}}  {'':<10} {'':>12} "
          f"{'':>16} {total_ns_per_run:>9.2f}")
    print()


def agg(dirs):
    # variant -> [ (set_label, ref_cnt, {prog: ns/pkt}) ]
    sets = {}
    for d in dirs:
        for fn in sorted(os.listdir(d)):
            if not fn.endswith(".bpfstats"):
                continue
            variant = fn[:-len(".bpfstats")]
            ref_cnt, per = summarize(os.path.join(d, fn))
            sets.setdefault(variant, []).append((d, ref_cnt, per))

    print(f"{'variant':<16} {'prog':<16} " +
          "".join(f"{d:>10} " for d in dirs) + "   total mean ± spread")
    print("-" * (34 + 11 * len(dirs) + 24))
    for variant, entries in sets.items():
        prog_names = []
        for _, _, per in entries:
            for name in per:
                if name not in prog_names:
                    prog_names.append(name)
        totals = []
        for _, _, per in entries:
            totals.append(sum(per.values()))
        mean = sum(totals) / len(totals)
        spread = (max(totals) - min(totals)) / 2 if len(totals) > 1 else 0.0
        first = True
        for name in prog_names:
            vals = [per.get(name, 0.0) for _, _, per in entries]
            row = f"{variant if first else '':<16} {name:<16} "
            row += "".join(f"{v:>10.1f} " for v in vals)
            if first:
                row += f"   {mean:>7.1f} ± {spread:<5.1f}"
            print(row)
            first = False
        print()


def main():
    args = sys.argv[1:]
    if args and args[0] == "--agg":
        agg(args[1:])
        return
    for path in args:
        show_file(path)


if __name__ == "__main__":
    if len(sys.argv) < 2:
        print(__doc__)
        sys.exit(1)
    main()
