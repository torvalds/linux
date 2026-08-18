#!/bin/bash
# Test that perf report handles truncated perf.data gracefully (no crash, no segfault — clean error exit).
# SPDX-License-Identifier: GPL-2.0
#
# Exercises the bounds checking and minimum-size validation added
# by the perf-data-validation hardening series.

err=0

cleanup() {
	[ -n "${perfdata}" ] && rm -f "${perfdata}" "${perfdata}.old"
	rm -f "${truncated}" "${stderrfile}"
	trap - EXIT TERM INT
}
trap 'cleanup; exit 1' TERM INT
trap cleanup EXIT

perfdata=$(mktemp /tmp/__perf_test.perf.data.XXXXX) || exit 2
truncated=$(mktemp /tmp/__perf_test.perf.data.XXXXX) || exit 2
stderrfile=$(mktemp /tmp/__perf_test.perf.data.XXXXX) || exit 2

# Record a simple workload
if ! perf record -o "${perfdata}" -- perf test -w noploop 2>/dev/null; then
	echo "Skip: perf record failed"
	cleanup
	exit 2
fi

file_size=$(wc -c < "${perfdata}")
if [ "${file_size}" -lt 512 ]; then
	echo "Skip: perf.data too small (${file_size} bytes)"
	cleanup
	exit 2
fi

# Test truncation at various offsets that exercise different
# parsing stages:
#   8    — file header magic only, no attrs or data
#   64   — partial file header (attr section incomplete)
#   256  — into the first events (partial event headers)
#   75%  — mid-stream truncation (partial event data)
for cut_at in 8 64 256 $((file_size * 3 / 4)); do
	if [ "${cut_at}" -ge "${file_size}" ]; then
		continue
	fi
	dd if="${perfdata}" of="${truncated}" bs="${cut_at}" count=1 2>/dev/null

	# perf report should exit with an error, not crash.
	# Capture stderr to detect sanitizer violations.
	perf report -i "${truncated}" --stdio > /dev/null 2> "${stderrfile}"
	exit_code=$?

	# A truncated file should never parse successfully
	if [ ${exit_code} -eq 0 ]; then
		echo "FAIL: perf report exited 0 (success) on ${cut_at}-byte truncated file — expected an error"
		err=1
		continue
	fi

	# Detect sanitizer violations — ASAN/MSAN/TSAN/UBSAN exit
	# with code 1 by default, which would otherwise look like a
	# clean error exit.  Check stderr for their markers.
	if grep -qE "^(==[0-9]+==ERROR:|SUMMARY: [A-Za-z]*Sanitizer)" "${stderrfile}" 2>/dev/null; then
		sanitizer=$(grep -oE "(Address|Memory|Thread|UndefinedBehavior)Sanitizer" "${stderrfile}" | head -1)
		echo "FAIL: perf report triggered ${sanitizer:-sanitizer} on ${cut_at}-byte truncated file"
		err=1
		continue
	fi

	# Detect crash signals portably — signal numbers differ
	# across architectures (e.g. SIGBUS is 7 on x86/ARM but
	# 10 on MIPS/SPARC).  Use kill -l to map the number to a
	# name on the running system.
	if [ ${exit_code} -gt 128 ] && [ ${exit_code} -lt 200 ]; then
		sig_name=$(kill -l $((exit_code - 128)) 2>/dev/null)
		case ${sig_name} in
		KILL|ILL|ABRT|BUS|FPE|SEGV|TRAP|SYS)
			echo "FAIL: perf report crashed (SIG${sig_name}) on ${cut_at}-byte truncated file"
			err=1
			;;
		esac
	fi
done

cleanup
exit ${err}
