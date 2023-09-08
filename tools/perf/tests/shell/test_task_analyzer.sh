#!/bin/bash
# perf script task-analyzer tests
# SPDX-License-Identifier: GPL-2.0

tmpdir=$(mktemp -d /tmp/perf-script-task-analyzer-XXXXX)
err=0

cleanup() {
  rm -f perf.data
  rm -f perf.data.old
  rm -f csv
  rm -f csvsummary
  rm -rf $tmpdir
  trap - exit term int
}

trap_cleanup() {
  cleanup
  exit 1
}
trap trap_cleanup exit term int

report() {
	if [ $1 = 0 ]; then
		echo "PASS: \"$2\""
	else
		echo "FAIL: \"$2\" Error message: \"$3\""
		err=1
	fi
}

check_exec_0() {
	if [ $? != 0 ]; then
		report 1 "invokation of ${$1} command failed"
	fi
}

find_str_or_fail() {
	grep -q "$1" $2
	if [ $? != 0 ]; then
		report 1 $3 "Failed to find required string:'${1}'."
	else
		report 0 $3
	fi
}

prepare_perf_data() {
	# 1s should be sufficient to catch at least some switches
	perf record -e sched:sched_switch -a -- sleep 1 > /dev/null 2>&1
}

# check standard inkvokation with no arguments
test_basic() {
	out="$tmpdir/perf.out"
	perf script report task-analyzer > $out
	check_exec_0 "perf"
	find_str_or_fail "Comm" $out ${FUNCNAME[0]}
}

test_ns_rename(){
	out="$tmpdir/perf.out"
	perf script report task-analyzer --ns --rename-comms-by-tids 0:random > $out
	check_exec_0 "perf"
	find_str_or_fail "Comm" $out ${FUNCNAME[0]}
}

test_ms_filtertasks_highlight(){
	out="$tmpdir/perf.out"
	perf script report task-analyzer --ms --filter-tasks perf --highlight-tasks perf \
	> $out
	check_exec_0 "perf"
	find_str_or_fail "Comm" $out ${FUNCNAME[0]}
}

test_extended_times_timelimit_limittasks() {
	out="$tmpdir/perf.out"
	perf script report task-analyzer --extended-times --time-limit :99999 \
	--limit-to-tasks perf > $out
	check_exec_0 "perf"
	find_str_or_fail "Out-Out" $out ${FUNCNAME[0]}
}

test_summary() {
	out="$tmpdir/perf.out"
	perf script report task-analyzer --summary > $out
	check_exec_0 "perf"
	find_str_or_fail "Summary" $out ${FUNCNAME[0]}
}

test_summaryextended() {
	out="$tmpdir/perf.out"
	perf script report task-analyzer --summary-extended > $out
	check_exec_0 "perf"
	find_str_or_fail "Inter Task Times" $out ${FUNCNAME[0]}
}

test_summaryonly() {
	out="$tmpdir/perf.out"
	perf script report task-analyzer --summary-only > $out
	check_exec_0 "perf"
	find_str_or_fail "Summary" $out ${FUNCNAME[0]}
}

test_extended_times_summary_ns() {
	out="$tmpdir/perf.out"
	perf script report task-analyzer --extended-times --summary --ns > $out
	check_exec_0 "perf"
	find_str_or_fail "Out-Out" $out ${FUNCNAME[0]}
	find_str_or_fail "Summary" $out ${FUNCNAME[0]}
}

test_csv() {
	perf script report task-analyzer --csv csv > /dev/null
	check_exec_0 "perf"
	find_str_or_fail "Comm;" csv ${FUNCNAME[0]}
}

test_csv_extended_times() {
	perf script report task-analyzer --csv csv --extended-times > /dev/null
	check_exec_0 "perf"
	find_str_or_fail "Out-Out;" csv ${FUNCNAME[0]}
}

test_csvsummary() {
	perf script report task-analyzer --csv-summary csvsummary > /dev/null
	check_exec_0 "perf"
	find_str_or_fail "Comm;" csvsummary ${FUNCNAME[0]}
}

test_csvsummary_extended() {
	perf script report task-analyzer --csv-summary csvsummary --summary-extended \
	>/dev/null
	check_exec_0 "perf"
	find_str_or_fail "Out-Out;" csvsummary ${FUNCNAME[0]}
}

prepare_perf_data
test_basic
test_ns_rename
test_ms_filtertasks_highlight
test_extended_times_timelimit_limittasks
test_summary
test_summaryextended
test_summaryonly
test_extended_times_summary_ns
test_csv
test_csvsummary
test_csv_extended_times
test_csvsummary_extended
cleanup
exit $err
