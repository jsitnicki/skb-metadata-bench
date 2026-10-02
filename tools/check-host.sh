#!/bin/bash
#
# Host-side sanity checks before benchmarking.
# Exits non-zero and prints what is wrong if the host is not in the
# expected measurement configuration.
#
# Checks:
#   - KVM halt-polling is disabled (halt_poll_ns=0); with polling on,
#     guest CPU accounting is skewed (idle-heavier runs get
#     under-accounted busy)
#   - E-core frequency clamp is in place (min == max) for the CPUs the
#     vCPUs are pinned to (12-19)
#
# Usage (on the host):
#   tools/check-host.sh && echo OK

set -o nounset

fail=0

err() {
	echo "FAIL: $*" >&2
	fail=1
}

# --- KVM halt-polling ---
hpv=/sys/module/kvm/parameters/halt_poll_ns
if [ -r "$hpv" ]; then
	ns=$(cat "$hpv")
	if [ "$ns" != "0" ]; then
		err "KVM halt polling is on (halt_poll_ns=$ns); disable with: echo 0 | sudo tee $hpv"
	fi
else
	echo "note: $hpv not readable (kvm module not loaded?); skipping halt-poll check" >&2
fi

# --- E-core frequency clamp (CPUs 12-19) ---
for cpu in /sys/devices/system/cpu/cpu{12..19}/cpufreq; do
	[ -d "$cpu" ] || continue
	min=$(cat "$cpu/scaling_min_freq")
	max=$(cat "$cpu/scaling_max_freq")
	if [ "$min" != "$max" ]; then
		err "${cpu} not clamped (min=$min max=$max); run: sudo tools/ecore-freq.sh 2700"
	fi
done

if [ "$fail" -eq 0 ]; then
	echo "host sanity checks OK"
fi
exit "$fail"
