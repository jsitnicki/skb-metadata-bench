#!/bin/bash

if [ $# -lt 1 ] || [ -z "$1" ]; then
	echo "usage: $0 <output-file>" >&2
	exit 1
fi

BW=50m

PERF_SEC=60
CLIENT_SEC=$((MPSTAT_SEC + 10))
SERVER_SEC=$((CLIENT_SEC + 10))

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

echo "Running perf..."
taskset -c 6 perf record -F 99 -C 0-5 -g -o /tmp/perf.data -- sleep $PERF_SEC

echo "Coping results..."
cp /tmp/perf.data "$1"

# Emit the derived artifacts next to the perf.data, following the
# naming from README.profile.md:
#   <out>.stacks          - perf script (for stackcollapse-perf.pl)
#   <out>.report.txt      - perf report --stdio
out="${1%.data}"
echo "Generating stacks..."
perf script --header -i /tmp/perf.data > "$out.stacks"
echo "Generating report..."
perf report --stdio -i /tmp/perf.data > "$out.report.txt"

echo "Wrote:"
echo "  $1"
echo "  $out.stacks"
echo "  $out.report.txt"
