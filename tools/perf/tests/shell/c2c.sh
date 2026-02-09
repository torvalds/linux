#!/bin/bash
# perf c2c tests
# SPDX-License-Identifier: GPL-2.0

set -e

err=0
perfdata=$(mktemp /tmp/__perf_c2c_test.perf.data.XXXXX)

cleanup() {
	rm -f "${perfdata}"
	rm -f "${perfdata}".old
	trap - EXIT TERM INT
}

trap_cleanup() {
	echo "Unexpected signal in ${FUNCNAME[1]}"
	cleanup
	exit 1
}
trap trap_cleanup EXIT TERM INT

check_c2c_support() {
	# Check if perf c2c record works.
	if ! perf c2c record -o "${perfdata}" -- true > /dev/null 2>&1 ; then
		return 1
	fi
	return 0
}

test_c2c_record_report() {
	echo "c2c record and report test"
	if ! check_c2c_support ; then
		echo "c2c record and report test [Skipped: perf c2c record failed (possibly missing hardware support)]"
		err=2
		return
	fi

	# Run a workload that does some memory operations.
	if ! perf c2c record -o "${perfdata}" -- perf test -w datasym 1 > /dev/null 2>&1 ; then
		echo "c2c record and report test [Skipped: perf c2c record failed during workload]"
		return
	fi

	if ! perf c2c report -i "${perfdata}" --stdio > /dev/null 2>&1 ; then
		echo "c2c record and report test [Failed: report failed]"
		err=1
		return
	fi

	if ! perf c2c report -i "${perfdata}" -N > /dev/null 2>&1 ; then
		echo "c2c record and report test [Failed: report -N failed]"
		err=1
		return
	fi

	echo "c2c record and report test [Success]"
}

test_c2c_record_report
cleanup
exit $err
