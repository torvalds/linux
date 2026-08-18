#!/bin/bash
# SPDX-License-Identifier: GPL-2.0
#
# Regression test for kmemleak's per-scan verbose dedup.
#
# Loads samples/kmemleak's helper module to generate orphan allocations
# (some of which share an allocation backtrace), runs a few kmemleak
# scans with verbose printing enabled, and verifies that no two
# "unreferenced object" reports within a single scan share the same
# backtrace - which would mean dedup failed to collapse them.
#
# This test is intentionally permissive: the kmemleak-test module's
# leaks frequently get reported across many separate scans (per-CPU
# chunk reuse, slab freelist pointers, kernel stack residue), so dedup
# may never have anything to fold within one scan. That is not a
# regression. The test only fails when it actually catches dedup not
# happening on input that should have triggered it - i.e. two reports
# with identical backtraces in the same scan.
#
# Author: Breno Leitao <leitao@debian.org>

ksft_skip=4
KMEMLEAK=/sys/kernel/debug/kmemleak
VERBOSE_PARAM=/sys/module/kmemleak/parameters/verbose
MODULE=kmemleak-test

skip() {
	echo "SKIP: $*"
	exit $ksft_skip
}

fail() {
	echo "FAIL: $*"
	exit 1
}

pass() {
	echo "PASS: $*"
	exit 0
}

[ "$(id -u)" -eq 0 ] || skip "must run as root"
[ -r "$KMEMLEAK" ] || skip "no kmemleak debugfs (CONFIG_DEBUG_KMEMLEAK)"
[ -w "$VERBOSE_PARAM" ] || skip "kmemleak verbose param missing"
modinfo "$MODULE" >/dev/null 2>&1 ||
	skip "$MODULE not built (CONFIG_SAMPLE_KMEMLEAK)"

# The verdict depends entirely on dmesg contents, so a silently-empty
# dmesg (dmesg_restrict=1 with CAP_SYSLOG dropped, restricted container,
# etc.) would let the script report PASS without parsing anything. Probe
# both read and clear up front and skip cleanly if either is denied.
dmesg >/dev/null 2>&1 ||
	skip "cannot read dmesg (need CAP_SYSLOG or dmesg_restrict=0)"
dmesg -C >/dev/null 2>&1 ||
	skip "cannot clear dmesg (need CAP_SYSLOG or dmesg_restrict=0)"

# kmemleak can be present but disabled at runtime (boot arg kmemleak=off,
# or it self-disabled after an internal error). In that state writes other
# than "clear" return EPERM, so probe once and skip if so.
if ! echo scan > "$KMEMLEAK" 2>/dev/null; then
	skip "kmemleak is disabled (check dmesg or kmemleak= boot arg)"
fi

prev_verbose=$(cat "$VERBOSE_PARAM")
# shellcheck disable=SC2317  # invoked indirectly via trap
cleanup() {
	echo "$prev_verbose" > "$VERBOSE_PARAM" 2>/dev/null
	rmmod "$MODULE" 2>/dev/null
	# Drain the leak set we generated. Subsequent selftests (e.g.
	# tools/testing/selftests/net/netfilter/nft_interface_stress.sh)
	# fail on any non-empty kmemleak report, so leaving the helper
	# module's intentional leaks behind would poison the rest of a
	# kselftest run.
	#
	# Caveat: kmemleak_clear() only greys objects that have already
	# been reported (OBJECT_REPORTED && unreferenced_object()). Helper
	# allocations that stayed "still referenced" throughout the test
	# (stale pointers in per-CPU chunks, slab freelists, kernel stacks)
	# were never reported and are therefore not greyed by this clear -
	# they remain tracked and a later scan can still surface them. Such
	# leftovers are inherent to the kmemleak-test sample module and are
	# not specific to this test; consumers that fail on any kmemleak
	# output (rather than on the test-specific backtraces) need to be
	# robust to that, or this test should be excluded from the run.
	echo clear > "$KMEMLEAK" 2>/dev/null
}
trap cleanup EXIT

echo 1 > "$VERBOSE_PARAM"

# Drain the existing leak set so the next scan only reports our objects.
echo clear > "$KMEMLEAK"

# Re-clear dmesg now (the up-front probe also cleared it, but anything
# logged between then and here - module unload chatter, the probe scan,
# the verbose-param write - would otherwise pollute the parse window).
dmesg -C >/dev/null

# If the module was left loaded by a previous aborted run, modprobe would
# be a no-op and the init function would not run, so no new leaks would be
# generated. Force a clean state first.
rmmod "$MODULE" 2>/dev/null
modprobe "$MODULE" || skip "failed to load $MODULE"
# Removing the module orphans the list elements without freeing them.
rmmod "$MODULE"    || skip "failed to unload $MODULE"

# Run a handful of scans so kmemleak has the chance to age and report
# the orphans. We do not require any particular number to be reported:
# the regression check below operates on whatever lands in dmesg.
#
# Note: with CONFIG_DEBUG_KMEMLEAK_AUTO_SCAN=y the kernel's own scan
# thread can report and mark these orphans (OBJECT_REPORTED) before our
# manual scans run, after which our scans will see nothing. The
# lower-bound check below catches the case where that happens and the
# manual scans also produce nothing.
SCAN_COUNT=4
SCAN_SLEEP=6
for _ in $(seq 1 "$SCAN_COUNT"); do
	echo scan > "$KMEMLEAK"
	sleep "$SCAN_SLEEP"
done

# Strip the leading "[   nnn.nnnnnn] " dmesg timestamp prefix. Without
# this, two identical stack frames printed from two reports in the same
# scan would produce different per-frame strings (different timestamps)
# and the duplicate-backtrace check below would not match them, silently
# passing a real dedup regression. Doing the strip here makes the rest
# of the parser timestamp-agnostic regardless of what dmesg defaults to.
log=$(dmesg | sed 's/^\[[^]]*\] //')

# After running the workload (modprobe + scans), dmesg should contain at
# least the helper module's pr_info lines and our manual-scan output. An
# empty capture here means dmesg succeeded earlier but is now denying us
# the buffer (race with dmesg_restrict toggling, etc.); refuse to give a
# verdict on no evidence.
[ -n "$log" ] || skip "dmesg returned empty after running workload"

# Lower bound: if kmemleak's own per-scan tally counted leaks but the
# verbose path emitted no "unreferenced object" line, the verbose printer
# itself is regressed - fail rather than silently passing on no input.
new_leaks=$(echo "$log" |
	sed -n 's/.*kmemleak: \([0-9]\+\) new suspected.*/\1/p' |
	awk '{s+=$1} END{print s+0}')
printed=$(echo "$log" | grep -c 'kmemleak: unreferenced object')
if [ "$new_leaks" -gt 0 ] && [ "$printed" -eq 0 ]; then
	fail "verbose path broken: $new_leaks leaks counted, 0 printed in $SCAN_COUNT scans"
fi

# Walk the log: split into per-scan chunks at "N new suspected memory
# leaks" boundaries; within each chunk, capture each "unreferenced
# object" report's backtrace and check that no backtrace is reported
# more than once. A duplicate within a single scan means dedup failed
# to collapse two leaks that share an allocation site.
violations=$(echo "$log" | awk '
	function flush_block() {
		if (in_block) {
			# Skip empty backtraces: leaks with trace_handle == 0
			# (early-boot allocations or stack_depot_save() failures
			# under memory pressure) are intentionally not deduped,
			# so multiple such reports in one scan are expected and
			# must not be flagged as a regression.
			if (bt != "")
				seen[bt]++
			in_block = 0
			collecting = 0
			bt = ""
		}
	}
	function check_and_reset(   b) {
		for (b in seen)
			if (seen[b] > 1)
				printf("backtrace seen %d times in one scan:\n%s\n",
				       seen[b], b)
		delete seen
	}
	# Scan boundary: the per-scan summary line.
	/kmemleak: [0-9]+ new suspected memory leaks/ {
		flush_block()
		check_and_reset()
		next
	}
	# Start of a new "unreferenced object" report.
	/kmemleak: unreferenced object/ {
		flush_block()
		in_block = 1
		next
	}
	# Inside a report, the "backtrace (crc ...):" line switches us to
	# backtrace-collecting mode.
	in_block && /kmemleak:[[:space:]]+backtrace \(crc/ {
		collecting = 1
		next
	}
	# Once collecting, capture only deeply-indented "kmemleak: " lines
	# (stack frames have 4+ spaces of indentation under "kmemleak: ";
	# headers and the "... and N more" tail line have less). This stops
	# unrelated kmemleak warns landing between reports from being lumped
	# into the backtrace key, which would mask a genuine duplicate.
	in_block && collecting && /kmemleak:[[:space:]]{4,}/ {
		bt = bt $0 "\n"
		next
	}
	END {
		flush_block()
		check_and_reset()
	}
')

if [ -n "$violations" ]; then
	echo "$violations"
	fail "kmemleak dedup regression: same backtrace reported more than once in a single scan"
fi

# Count the dedup summary lines so the report distinguishes "dedup
# actually fired" from "no same-backtrace leaks turned up to dedup".
dedup_lines=$(echo "$log" | grep -c 'more object(s) with the same backtrace')

if [ "$dedup_lines" -gt 0 ]; then
	pass "no dedup violations across $SCAN_COUNT scans; dedup fired ($dedup_lines summary line(s) observed)"
else
	pass "no dedup violations across $SCAN_COUNT scans; dedup had nothing to collapse"
fi
