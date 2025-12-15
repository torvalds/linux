#!/bin/bash
# perf timechart tests
# SPDX-License-Identifier: GPL-2.0

set -e

err=0
perfdata=$(mktemp /tmp/__perf_timechart_test.perf.data.XXXXX)
output=$(mktemp /tmp/__perf_timechart_test.output.XXXXX.svg)

cleanup() {
	rm -f "${perfdata}"
	rm -f "${output}"
	trap - EXIT TERM INT
}

trap_cleanup() {
	echo "Unexpected signal in ${FUNCNAME[1]}"
	cleanup
	exit 1
}
trap trap_cleanup EXIT TERM INT

test_timechart() {
	echo "Basic perf timechart test"

	# Try to record timechart data.
	# perf timechart record uses system-wide recording and specific tracepoints.
	# If it fails (e.g. permissions, missing tracepoints), skip the test.
	if ! perf timechart record -o "${perfdata}" true > /dev/null 2>&1; then
		echo "Basic perf timechart test [Skipped: perf timechart record failed (permissions/events?)]"
		return
	fi

	# Generate the timechart
	if ! perf timechart -i "${perfdata}" -o "${output}" > /dev/null 2>&1; then
		echo "Basic perf timechart test [Failed: perf timechart command failed]"
		err=1
		return
	fi

	# Check if output file exists and is not empty
	if [ ! -s "${output}" ]; then
		echo "Basic perf timechart test [Failed: output file is empty or missing]"
		err=1
		return
	fi

	# Check if it looks like an SVG
	if ! grep -q "svg" "${output}"; then
		echo "Basic perf timechart test [Failed: output doesn't look like SVG]"
		err=1
		return
	fi

	echo "Basic perf timechart test [Success]"
}

if ! perf check feature -q libtraceevent ; then
	echo "perf timechart is not supported. Skip."
        cleanup
	exit 2
fi

test_timechart
cleanup
exit $err
