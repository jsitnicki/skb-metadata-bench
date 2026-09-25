#!/bin/bash

if [ $# -lt 1 ] || [ -z "$1" ]; then
	echo "usage: $0 <output-file>" >&2
	exit 1
fi

BW=14m

MPSTAT_SEC=60
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

echo "Running mpstat..."
taskset -c 6 mpstat -o JSON -P 0-5 1 $MPSTAT_SEC > /tmp/mpstat.log

echo "Coping results..."
cp /tmp/mpstat.log $1
