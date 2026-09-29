```
root@virtme-ng:/home/jakub/cf/tasks/bpf-meta-in-skb-ext/benchmark# ./attach bpf/ext.bpf.o
tc egress attached on lo
tc ingress attached on lo
root@virtme-ng:/home/jakub/cf/tasks/bpf-meta-in-skb-ext/benchmark# ./profile.sh results/perf.ext.data
Running servers...
Running clients...
Running perf...
[ perf record: Woken up 12 times to write data ]
[ perf record: Captured and wrote 3.275 MB /tmp/perf.data (24666 samples) ]
Coping results...
root@virtme-ng:/home/jakub/cf/tasks/bpf-meta-in-skb-ext/benchmark# perf script --header -i /tmp/perf.data > results/perf.ext.stacks
root@virtme-ng:/home/jakub/cf/tasks/bpf-meta-in-skb-ext/benchmark# perf report --stdio -i /tmp/perf.data > results/perf.ext.txt
root@virtme-ng:/home/jakub/cf/tasks/bpf-meta-in-skb-ext/benchmark#
```

```
$ stackcollapse-perf.pl < perf.ext.stacks | flamegraph.pl --hash > flamegraph-cpu-ext.svg
```
