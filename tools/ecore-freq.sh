#!/bin/bash
# Pin E-core (CPUs 12-19) frequency on the benchmark host.
#
# Active-mode intel_pstate has no performance governor, so pinning is
# done by clamping scaling_min_freq == scaling_max_freq (kHz).
#
# Usage:
#   tools/ecore-freq.sh <khz>   - fix E-core frequency (e.g. 4000000)
#   tools/ecore-freq.sh off     - restore dynamic scaling
#
# Needs root (writes to /sys cpufreq). Not persistent across reboot.

set -euo pipefail

ECORES="12 13 14 15 16 17 18 19"
SYSFS=/sys/devices/system/cpu

# Hardware limits, from cpuN/cpufreq/cpuinfo_{min,max}_freq
read -r HW_MIN <"$SYSFS/cpu${ECORES%% *}/cpufreq/cpuinfo_min_freq"
read -r HW_MAX <"$SYSFS/cpu${ECORES%% *}/cpufreq/cpuinfo_max_freq"

usage() {
	sed -n '2,12p' "$0"
	echo
	echo "hardware limits: min=$HW_MIN max=$HW_MAX kHz"
	exit "${1:-0}"
}

[[ $# -eq 1 ]] || usage 1

if [[ $1 == "off" ]]; then
	MIN=$HW_MIN
	MAX=$HW_MAX
else
	[[ $1 =~ ^[0-9]+$ ]] || usage 1
	MIN=$1
	MAX=$1
	(( MIN >= HW_MIN && MAX <= HW_MAX )) || {
		echo "error: $1 outside $HW_MIN..$HW_MAX kHz" >&2
		exit 1
	}
fi

for c in $ECORES; do
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

first=${ECORES%% *}
last=${ECORES##* }
echo "E-cores (cpu$first-$last): min=$MIN max=$MAX kHz"
paste -d' ' <(grep "^processor" /proc/cpuinfo) <(grep MHz /proc/cpuinfo) |
	sed -n "$((first + 1)),$((last + 1))p"
