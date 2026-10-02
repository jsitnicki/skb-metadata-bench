#!/bin/bash
#
# Trace rhashtable resize machinery in the guest using kprobe events
# (guest kernel lacks CONFIG_FUNCTION_PROFILER, so ftrace profiling is
# unavailable). Counts calls to the shrink/rehash paths over a sampling
# window (default 10 s), e.g. while a benchmark is running.
#
# Usage (inside the guest):
#   tools/trace-rhashtable.sh [seconds]
#
# Prints call counts per function. Zero counts for rhashtable_shrink /
# rhashtable_rehash_alloc mean the resize machinery is quiet; nonzero
# counts mean the table is churning.

set -o nounset

SECS="${1:-10}"
TR=/sys/kernel/debug/tracing

if [ ! -d "$TR" ]; then
	echo "tracefs not available: $TR missing (mount -t debugfs none /sys/kernel/debug)" >&2
	exit 1
fi

# Resize-path functions: static, but real symbols in kallsyms.
# (rht_grow_above_75/rht_shrink_below_30 are static inline - no symbol.)
FUNCS="
rhashtable_shrink
rhashtable_rehash_alloc
rhashtable_rehash_chain
rht_deferred_worker
"

added=""
cleanup() {
	for f in $added; do
		echo 0 > "$TR/events/kprobes/$f/enable" 2>/dev/null
		echo "-:kprobes/$f" >> "$TR/kprobe_events" 2>/dev/null
	done
}
trap cleanup EXIT

for f in $FUNCS; do
	if ! grep -qw "$f" "$TR/available_filter_functions" 2>/dev/null &&
	   ! grep -qw "$f" /proc/kallsyms 2>/dev/null; then
		echo "note: $f not found (inlined?)" >&2
		continue
	fi
	if echo "p:$f" >> "$TR/kprobe_events"; then
		echo 1 > "$TR/events/kprobes/$f/enable"
		added="$added $f"
	else
		echo "note: cannot probe $f" >&2
	fi
done

if [ -z "$added" ]; then
	echo "no traceable functions found" >&2
	exit 1
fi

echo "counting calls for ${SECS}s..."
sleep "$SECS"

for f in $added; do
	# kprobe_profile line: "p:kprobes/<event> <symbol> <nhit> <nmissed>"
	hits=$(awk -v name="$f" '$1 == "p:kprobes/"name { print $(NF-1) }' "$TR/kprobe_profile")
	printf "%-28s %s calls\n" "$f" "${hits:-0}"
done
