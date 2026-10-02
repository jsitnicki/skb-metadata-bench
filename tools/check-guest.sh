#!/bin/bash
#
# Guest-side sanity checks before benchmarking.
# Exits non-zero and prints what is wrong if the guest is not in the
# expected measurement configuration.
#
# Checks:
#   - lockdep is disabled (CONFIG_LOCKDEP compiled out or off)
#
# Usage (inside the guest):
#   tools/check-guest.sh && echo OK

set -o nounset

fail=0

err() {
	echo "FAIL: $*" >&2
	fail=1
}

# Lockdep splats go to dmesg with "INFO: lockdep is turned on." only
# when it gets *disabled at runtime*; when compiled in and active, the
# reliable signals are:
#   1. /proc/cmdline: lockdep=off would disable it
#   2. debugfs: /sys/kernel/debug/lockdep exists only when active
#   3. /proc/config.gz or /boot/config-$(uname -r): CONFIG_LOCKDEP=y
# We check all we can.

# 1. Runtime disable flag (if present, lockdep is off regardless)
if grep -qw 'lockdep=off' /proc/cmdline; then
	exit 0
fi

# 2. debugfs lockdep dir -> compiled in and live
if [ -d /sys/kernel/debug/lockdep ]; then
	err "lockdep is active (/sys/kernel/debug/lockdep exists); rebuild without CONFIG_LOCKDEP"
fi

# 3. Config check
cfg=""
if [ -r /proc/config.gz ]; then
	cfg=$(zcat /proc/config.gz)
elif [ -r "/boot/config-$(uname -r)" ]; then
	cfg=$(cat "/boot/config-$(uname -r)")
fi

if [ -n "$cfg" ]; then
	if grep -q '^CONFIG_LOCKDEP=y' <<<"$cfg"; then
		err "CONFIG_LOCKDEP=y in kernel config; rebuild without it"
	fi
else
	# No config readable and no debugfs dir: probably fine, but say so.
	echo "note: kernel config not readable; lockdep check based on debugfs only" >&2
fi

if [ "$fail" -eq 0 ]; then
	echo "guest sanity checks OK"
fi
exit "$fail"
