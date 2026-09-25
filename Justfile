# gated skb tracepoints
gtp_tree := "~/src/linux"
# skb extension for bpf
ext_tree := "~/src/linux-skb-ext"

_vm RUNDIR *ARGS:
    vng --run {{RUNDIR}} --console --cpus 8 --rwdir results/ \
    --qemu-opts="-qmp unix:/tmp/qemu.sock,server,nowait" -- {{ARGS}}

vm-gtp *ARGS:
    just _vm {{gtp_tree}} {{ARGS}}

vm-ext *ARGS:
    just _vm {{ext_tree}} {{ARGS}}

pin:
    python tools/pin-vcpus-threads.py -s /tmp/qemu.sock 12-19
