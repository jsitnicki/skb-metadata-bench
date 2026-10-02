#!/bin/bash

set -o nounset

BPF_OBJ="$1"
OUT_FILE="$2"

# Refuse to benchmark on a misconfigured guest (e.g. lockdep on).
tools/check-guest.sh || exit 1

./attach "$BPF_OBJ"
./profile.sh "$OUT_FILE"

# Self-document whether metadata actually flowed during the run.
# Degenerate ext runs (delta ~0 or negative vs floor) correlate with
# FOUND not tracking INGRESS or EGRESS counts collapsing; log enough
# state to tell CPU-side (cache/freq) from workload-side (attach/GRO)
# failures. Appended next to the perf.data as <name>.log.
LOG_FILE="${OUT_FILE%.data}.log"
{
	echo "# $(date -Is) $BPF_OBJ -> $OUT_FILE"
	echo "# uname: $(uname -r)"
	echo "# link stats (lo):"
	ip -s link show dev lo
	echo "# bpf stats map:"
	python3 tools/show_stats.py || true
} >"$LOG_FILE"
echo "wrote $LOG_FILE"
