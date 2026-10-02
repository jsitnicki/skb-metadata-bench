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

# Generate a flamegraph SVG from a perf script stacks file:
#   just flamegraph results/perf.gtp-1m.stacks
# Writes <stacks basename minus .stacks>.svg next to the input.
flamegraph STACKS:
    stackcollapse-perf.pl < {{STACKS}} | flamegraph.pl --hash > {{without_extension(STACKS)}}.svg
    @echo "wrote {{without_extension(STACKS)}}.svg"

# Per-packet BPF cost (ns/pkt) from prog stats across repetition sets.
# Each dir is one set of <tree>-<variant>.bpfstats files; ns/pkt per
# prog is normalized to the set's busiest tc prog run_cnt, variant
# TOTAL is mean ± spread across the sets. Default: the three sets.
#   just bpfstats                    # results-1 results-2 results-3
#   just bpfstats results-1          # a single set
bpfstats *DIRS:
    python3 tools/bpfstats.py --agg {{ if DIRS == "" { "results-1 results-2 results-3" } else { DIRS } }}
