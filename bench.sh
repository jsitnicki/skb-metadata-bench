#!/bin/bash

# Runs the 3-pair loopback UDP load + mpstat, and also collects
# per-program BPF stats (run_time_ns / run_cnt) on top: the sysctl
# kernel.bpf_stats_enabled is turned on for the whole window and the
# prog snapshot is written to <output-base>.bpfstats next to the JSON.
# Parse the sidecar with tools/bpfstats.py for per-prog ns/run.

if [ $# -lt 1 ] || [ -z "$1" ]; then
	echo "usage: $0 <output-file>" >&2
	exit 1
fi

BW=50m

MPSTAT_SEC=60
CLIENT_SEC=$((MPSTAT_SEC + 10))
SERVER_SEC=$((CLIENT_SEC + 10))

STATS_CTL=/proc/sys/kernel/bpf_stats_enabled
OUT_BPFSTATS="${1%.json}.bpfstats"

if [ -w "$STATS_CTL" ]; then
	echo 1 > "$STATS_CTL"
	echo "bpf stats enabled ($(cat $STATS_CTL))"
else
	echo "warning: $STATS_CTL not writable; skipping bpf stats" >&2
fi

echo "Running servers..."
taskset -c 0 iperf -u -s -1 -t $SERVER_SEC -B 127.1.1.1 > /tmp/iperf-server.1.log &
taskset -c 2 iperf -u -s -1 -t $SERVER_SEC -B 127.2.2.2 > /tmp/iperf-server.2.log &
taskset -c 4 iperf -u -s -1 -t $SERVER_SEC -B 127.3.3.3 > /tmp/iperf-server.3.log &

sleep 5

echo "Running clients..."
taskset -c 1 iperf -u -c 127.1.1.1 -b $BW -l 36 -t $CLIENT_SEC > /tmp/iperf-client.1.log &
taskset -c 3 iperf -u -c 127.2.2.2 -b $BW -l 36 -t $CLIENT_SEC > /tmp/iperf-client.2.log &
taskset -c 5 iperf -u -c 127.3.3.3 -b $BW -l 36 -t $CLIENT_SEC > /tmp/iperf-client.3.log &

sleep 5

echo "Running mpstat..."
taskset -c 6 mpstat -o JSON -P 0-5 1 $MPSTAT_SEC > /tmp/mpstat.log

echo "Coping results..."
cp /tmp/mpstat.log $1

if [ -w "$STATS_CTL" ]; then
	{
		echo "# $(date -Is) bpf prog stats (run_time_ns / run_cnt)"
		bpftool -j prog show
	} > "$OUT_BPFSTATS"
	echo 0 > "$STATS_CTL"
	echo "wrote $OUT_BPFSTATS"
fi
