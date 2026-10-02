#!/usr/bin/env python3
"""Aggregate CPU usage stats from mpstat -o JSON files.

For each file, computes across all sampled CPUs and all samples:

  - total CPU usage (100 - idle) mean and relative deviation
    (population stddev / mean, computed over per-sample totals)
  - breakdown by CPU time type (usr/sys/soft/...), as % of all
    CPU time, each with mean + relative deviation

With two files, also prints the plain delta. With four files (two
baseline/test pairs), also prints markdown results and delta tables
for the benchmark report; deltas carry ±1 stddev propagated in
quadrature from the per-run rdevs.

Usage: cpustats.py run-0.json [run-1.json ...]
"""

import argparse
import json
import math
import sys

TYPES = ["usr", "nice", "sys", "iowait", "irq", "soft", "steal", "guest", "gnice"]


def load_samples(path: str) -> list[dict]:
    """Return one dict per sample: {type: total_%}, plus 'busy' key."""
    data = json.load(open(path))
    stats = data["sysstat"]["hosts"][0]["statistics"]
    samples = []
    for snap in stats:
        tot = dict.fromkeys(TYPES, 0.0)
        busy = 0.0
        for cpu in snap["cpu-load"]:
            if cpu["cpu"] == "all":
                continue
            busy += 100.0 - cpu["idle"]
            for t in TYPES:
                tot[t] += cpu.get(t, 0.0)
        tot["busy"] = busy
        samples.append(tot)
    if not samples:
        raise SystemExit(f"{path}: no samples")
    return samples


def mean_rdev(samples: list[dict], key: str) -> tuple[float, float]:
    vals = [s[key] for s in samples]
    mean = math.fsum(vals) / len(vals)
    if mean == 0:
        return 0.0, 0.0
    var = math.fsum((v - mean) ** 2 for v in vals) / len(vals)
    return mean, math.sqrt(var) / mean


def report(path: str, ncpus: int) -> dict:
    samples = load_samples(path)
    rows = {}
    for key in ["busy"] + TYPES:
        mean, rdev = mean_rdev(samples, key)
        # convert summed-over-cpus % to fraction of one cpu
        rows[key] = (mean / (100.0 * ncpus), rdev)
    return rows


def main() -> None:
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("files", nargs="+", help="mpstat -o JSON files")
    ap.add_argument("--pps", type=float, default=520833,
                    help="packet rate for the ns/pkt row (default: %(default)s)")
    ap.add_argument("--strip-prefix", default="results/",
                    help="prefix stripped from file names in md tables "
                         "(default: %(default)s)")
    ap.add_argument("--no-md", action="store_true",
                    help="skip markdown tables for four-file runs")
    args = ap.parse_args()

    results = {}
    ncpus = {}
    for f in args.files:
        samples = load_samples(f)
        ncpu = len(json.load(open(f))["sysstat"]["hosts"][0]
                   ["statistics"][0]["cpu-load"])
        ncpus[f] = ncpu
        results[f] = report(f, ncpu)

    hdr = f"{'type':<8}"
    for f in args.files:
        hdr += f" | {f:>22}"
    print(hdr)
    print("-" * len(hdr))
    for key in ["busy"] + TYPES:
        line = f"{key:<8}"
        for f in args.files:
            mean, rdev = results[f][key]
            line += f" | {mean * 100:9.2f}% (rdev {rdev:5.1%})"
        print(line)

    # delta for exactly two files: busy and the top contributor types
    if len(args.files) == 2:
        a, b = args.files
        print()
        print(f"delta ({b} - {a}):")
        for key in ["busy", "usr", "sys", "soft"]:
            d = results[b][key][0] - results[a][key][0]
            print(f"  {key:<6} {d * 100:+6.2f}%-cpu "
                  f"(= {d * ncpus[b] * 100:8.0f} aggregate % across {ncpus[b]} cpus)")

    # markdown report tables for four files: two baseline/test pairs,
    # e.g. gtp-cnt gtp-gtp ext-cnt ext-ext
    if len(args.files) == 4 and not args.no_md:
        a, b, c, d = args.files
        label = {f: f.removeprefix(args.strip_prefix) for f in args.files}

        def sigma(f, key):
            mean, rdev = results[f][key]
            return mean * rdev

        def delta(base, test, key):
            dmean = results[test][key][0] - results[base][key][0]
            dsd = math.hypot(sigma(base, key), sigma(test, key))
            return dmean, dsd

        print()
        print(f"| | {label[a]} (floor) | {label[b]} | "
              f"| {label[c]} (floor) | {label[d]} |")
        print("|---|---|---|---|---|---|")
        for key in ["busy", "sys", "soft"]:
            row = f"| {key} "
            for f in args.files:
                mean, rdev = results[f][key]
                row += f"| {mean * 100:.2f} ({rdev:.1%}) "
                if f == b:
                    row += "| "
            row += "|"
            print(row)

        print()
        print(f"| | {label[b]} | {label[d]} |")
        print("|---|---|---|")
        for key in ["busy", "sys", "soft"]:
            m1, s1 = delta(a, b, key)
            m2, s2 = delta(c, d, key)
            b1 = "**" if key == "busy" else ""
            row = f"| {b1}{key}{b1} "
            row += f"| {b1}{m1 * 100:+.2f} ± {s1 * 100:.2f}{b1} "
            row += f"| {b1}{m2 * 100:+.2f} ± {s2 * 100:.2f}{b1} |"
            print(row)
        if args.pps:
            n1 = delta(a, b, "busy")[0] / args.pps * 1e9
            n2 = delta(c, d, "busy")[0] / args.pps * 1e9
            print(f"| ns/pkt @{args.pps / 1000:.0f}k pps "
                  f"| **+≈ {n1:.0f}** | **+≈ {n2:.0f}** |")


if __name__ == "__main__":
    sys.exit(main())
