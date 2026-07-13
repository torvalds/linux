#!/bin/bash
# SPDX-License-Identifier: GPL-2.0
#
# Functional test for kmemleak's N-consecutive-scan leak confirmation
# (the min_unref_scans module parameter).
#
# kmemleak only reports an object once it has stayed unreferenced for
# min_unref_scans consecutive scans. The default of 1 reports on the first
# scan (historical behaviour); higher values filter transient false
# positives where a live object's only reference is briefly invisible to a
# single scan (e.g. an RCU tree update in flight while the scan runs). The
# test loads samples/kmemleak's helper module to create orphan allocations
# and, counting only those orphans (matched by their [kmemleak_test]
# backtrace so unrelated leaks already present on the system are ignored),
# checks that:
#   - a freshly allocated object is greyed on its first scan (its checksum
#     settles then), so nothing can be reported before that priming scan;
#     each case below primes once first,
#   - with the default threshold (min_unref_scans=1) one scan after priming
#     reports the orphans,
#   - raising the threshold to 2 needs two scans after priming: one is not
#     enough, the second reports,
#   - the parameter reads back what was written.
#
# The "one post-prime scan is not enough at min_unref_scans=2" check is the
# core regression test: raising min_unref_scans must push the report
# strictly later. Like ksft_kmemleak_dedup.sh, if the module yields no
# detectable orphan at all in the running environment the test skips rather
# than failing.
#
# Author: Breno Leitao <leitao@debian.org>

# KTAP output helpers (ktap_skip_all, ktap_exit_fail_msg, ktap_test_pass, ...).
DIR="$(dirname "$(readlink -f "$0")")"
# shellcheck source=../kselftest/ktap_helpers.sh
source "${DIR}"/../kselftest/ktap_helpers.sh

KMEMLEAK=/sys/kernel/debug/kmemleak
PARAM=/sys/module/kmemleak/parameters/min_unref_scans
MODULE=kmemleak-test
AGE=6		# seconds; must exceed kmemleak's 5s minimum object age

ktap_print_header

[ "$(id -u)" -eq 0 ] || { ktap_skip_all "must run as root"; exit "$KSFT_SKIP"; }
[ -r "$KMEMLEAK" ] ||
	{ ktap_skip_all "no kmemleak debugfs (CONFIG_DEBUG_KMEMLEAK)"; exit "$KSFT_SKIP"; }
[ -w "$PARAM" ] ||
	{ ktap_skip_all "min_unref_scans module parameter not present"; exit "$KSFT_SKIP"; }
modinfo "$MODULE" >/dev/null 2>&1 ||
	{ ktap_skip_all "$MODULE not built (CONFIG_SAMPLE_KMEMLEAK)"; exit "$KSFT_SKIP"; }

# kmemleak can be present but disabled at runtime (kmemleak=off boot arg,
# or it self-disabled after an internal error); a "scan" then returns
# EPERM. Probe once and skip if so.
echo scan > "$KMEMLEAK" 2>/dev/null ||
	{ ktap_skip_all "kmemleak is disabled (check dmesg or kmemleak= boot arg)"; exit "$KSFT_SKIP"; }

prev=$(cat "$PARAM")
# shellcheck disable=SC2317  # invoked indirectly via trap
cleanup() {
	echo "$prev" > "$PARAM" 2>/dev/null		# restore the parameter
	echo scan=on > "$KMEMLEAK" 2>/dev/null		# re-enable auto scan
	rmmod "$MODULE" 2>/dev/null
	echo clear > "$KMEMLEAK" 2>/dev/null
}
trap cleanup EXIT

# Stop the automatic scan thread: only our manual scans should advance an
# object's consecutive-unreferenced run. An auto scan landing between two
# manual scans would change the result and make the test flaky.
echo scan=off > "$KMEMLEAK" 2>/dev/null

# Create a fresh, aged set of orphan objects from the helper module's init
# path (its kmalloc/vmalloc/percpu allocations are dropped right away).
# Pre-existing reported leaks are greyed first ("clear") so only our
# orphans are counted. The module is left loaded on purpose: once it is
# unloaded its symbols are gone, so the orphan backtraces no longer resolve
# to [kmemleak_test] and could not be matched below.
gen_orphans() {
	rmmod "$MODULE" 2>/dev/null
	echo clear > "$KMEMLEAK"
	modprobe "$MODULE" ||
		{ ktap_skip_all "failed to load $MODULE"; exit "$KSFT_SKIP"; }
	sleep "$AGE"
}

scan() { echo scan > "$KMEMLEAK"; }

# Number of helper-module orphans currently reported by kmemleak. Matching
# the module's own backtrace ([kmemleak_test]) keeps the count immune to
# unrelated leaks on the running system. kmemleak only lists an object here
# once it has been reported, so this reflects the confirmation gating.
count_orphans() {
	c=$(grep -c '\[kmemleak_test\]' "$KMEMLEAK" 2>/dev/null)
	echo "${c:-0}"
}

# 0) the parameter reads back what was written.
echo 3 > "$PARAM"
[ "$(cat "$PARAM")" = "3" ] || ktap_exit_fail_msg "min_unref_scans did not read back as 3"

# Priming scan: kmemleak greys a freshly allocated object on its first scan
# (its checksum settles then), so nothing can be reported until a second
# scan. Every case below runs this priming scan before counting.
prime() { scan; }

# 1) min_unref_scans=1 (default): one scan after priming reports the
#    orphans. This also establishes that the helper produces detectable
#    orphans here.
echo 1 > "$PARAM"
gen_orphans
prime
scan
first=$(count_orphans)
[ "$first" -gt 0 ] ||
	{ ktap_skip_all "$MODULE produced no detectable orphans (cannot test min_unref_scans)"; exit "$KSFT_SKIP"; }

# 2) min_unref_scans=2: after priming, one scan is not enough (still
#    gated), the second reports. The gated-scan-zero check is the core
#    regression.
echo 2 > "$PARAM"
gen_orphans
prime
scan; s1=$(count_orphans)
scan; s2=$(count_orphans)
[ "$s1" -eq 0 ] || ktap_exit_fail_msg "min_unref_scans=2: $s1 orphan(s) after 1 post-prime scan (must be 0)"
[ "$s2" -gt 0 ] || ktap_exit_fail_msg "min_unref_scans=2: no report after 2 post-prime scans (false negative)"

ktap_set_plan 1
ktap_test_pass "min_unref_scans=1 reported $first orphan(s) one scan after priming; =2 held them one scan longer ($s1 after one scan, $s2 after two); param read-back ok"
ktap_finished
