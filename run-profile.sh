#!/bin/bash

set -o nounset

tree="$1"
bpf_obj="$2"
out_name="${3:-$tree-$bpf_obj}"

# Refuse to benchmark from a misconfigured host (e.g. halt polling on).
tools/check-host.sh || exit 1

(
    for i in {1..5}; do
        [ -S /tmp/qemu.sock ] && break
        sleep 1
    done

    if [ ! -S /tmp/qemu.sock ]; then
        echo "timeout waiting for /tmp/qemu.sock"
    else
        just pin
    fi
) &

just vm-$tree ./attach-and-profile.sh bpf/$bpf_obj.bpf.o results/perf.$out_name.data
