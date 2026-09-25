#!/usr/bin/env python3
"""Dump nonzero per-cpu values from the stats map with named keys.

Usage: show_stats.py [map-name] [key0name key1name ...]
Defaults: map 'stats', keys from cnt/gtp/ext variants.
"""

import json
import subprocess
import sys

NAMES = {
    # shared: cnt (0/1), gtp (0-3), ext (0-6)
    0: "INGRESS", 1: "EGRESS",
    2: "FREE_REAPED/CREATE_FAIL", 3: "COPY/FIND_FAIL",
    4: "SLICE_FAIL", 5: "BADMAGIC", 6: "FOUND",
}

mapname = sys.argv[1] if len(sys.argv) > 1 else "stats"
out = subprocess.run(
    ["bpftool", "-j", "map", "dump", "name", mapname],
    capture_output=True, text=True, check=True).stdout

def elements(maps):
    for m in maps:
        if "elements" in m:          # multiple maps matched: map objects
            yield from m["elements"]
        elif "key" in m:             # single map matched: bare elements
            yield m


for e in elements(json.loads(out)):
    e = e.get("formatted", e)  # bpftool -j wraps parsed values
    vals = [(v["cpu"], v["value"]) for v in e["values"] if v["value"] > 0]
    if vals:
        total = sum(v for _, v in vals)
        print(f"{NAMES.get(e['key'], e['key']):<24} total={total:<10} {vals}")
