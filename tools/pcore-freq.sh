#!/bin/bash
# Pin P-core (CPUs 0-11) frequency on the benchmark host.
#
# Active-mode intel_pstate has no performance governor, so pinning is
# done by clamping scaling_min_freq == scaling_max_freq (kHz).
#
# Usage:
#   tools/pcore-freq.sh <mhz>   - fix P-core frequency (e.g. 3800)
#   tools/pcore-freq.sh off     - restore dynamic scaling
#
# Needs root (writes to /sys cpufreq). Not persistent across reboot.
# P-cores turbo higher than the non-turbo max; clamping above
# cpuinfo_max_freq requires no_turbo=0 and is capped by HWP.

set -euo pipefail

PCORES="0 1 2 3 4 5 6 7 8 9 10 11"
SYSFS=/sys/devices/system/cpu

# Hardware limits, from cpuN/cpufreq/cpuinfo_{min,max}_freq
read -r HW_MIN <"$SYSFS/cpu${PCORES%% *}/cpufreq/cpuinfo_min_freq"
read -r HW_MAX <"$SYSFS/cpu${PCORES%% *}/cpufreq/cpuinfo_max_freq"

usage() {
	sed -n '2,13p' "$0"
	echo
	echo "hardware limits: min=$((HW_MIN / 1000)) max=$((HW_MAX / 1000)) MHz"
	exit "${1:-0}"
}

[[ $# -eq 1 ]] || usage 1

if [[ $1 == "off" ]]; then
	MIN=$HW_MIN
	MAX=$HW_MAX
else
	[[ $1 =~ ^[0-9]+$ ]] || usage 1
	MIN=$(( $1 * 1000 ))
	MAX=$MIN
	(( MIN >= HW_MIN && MAX <= HW_MAX )) || {
		echo "error: $1 outside $((HW_MIN / 1000))..$((HW_MAX / 1000)) MHz" >&2
		exit 1
	}
fi

for c in $PCORES; do
	d="$SYSFS/cpu$c/cpufreq"
	if (( MIN > $(<"$d/scaling_max_freq") )); then
		# raising: bump max first so min <= max holds
		echo "$MAX" >"$d/scaling_max_freq"
		echo "$MIN" >"$d/scaling_min_freq"
	else
		# lowering: drop min first
		echo "$MIN" >"$d/scaling_min_freq"
		echo "$MAX" >"$d/scaling_max_freq"
	fi
done

first=${PCORES%% *}
last=${PCORES##* }
echo "P-cores (cpu$first-$last): min=$((MIN / 1000)) max=$((MAX / 1000)) MHz"
paste -d' ' <(grep "^processor" /proc/cpuinfo) <(grep MHz /proc/cpuinfo) |
	sed -n "$((first + 1)),$((last + 1))p"
