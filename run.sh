#!/bin/bash

set -o nounset

tree="$1"
bpf_obj="$2"

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

just vm-$tree ./attach-and-bench.sh bpf/$bpf_obj.bpf.o results/$tree-$bpf_obj.json
