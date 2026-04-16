#!/bin/bash
# perf script perl tests
# SPDX-License-Identifier: GPL-2.0

set -e

# set PERF_EXEC_PATH to find scripts in the source directory
perfdir=$(dirname "$0")/../..
if [ -e "$perfdir/scripts/perl/Perf-Trace-Util" ]; then
  export PERF_EXEC_PATH=$perfdir
fi


perfdata=$(mktemp /tmp/__perf_test_script_perl.perf.data.XXXXX)
generated_script=$(mktemp /tmp/__perf_test_script.XXXXX.pl)

cleanup() {
  rm -f "${perfdata}"
  rm -f "${generated_script}"
  trap - EXIT TERM INT
}

trap_cleanup() {
  echo "Unexpected signal in ${FUNCNAME[1]}"
  cleanup
  exit 1
}
trap trap_cleanup TERM INT
trap cleanup EXIT

check_perl_support() {
	if perf check feature -q libperl; then
		return 0
	fi
	echo "perf script perl test [Skipped: no libperl support]"
	return 2
}

test_script() {
	local event_name=$1
	local expected_output=$2
	local record_opts=$3

	echo "Testing event: $event_name"

	# Try to record. If this fails, it might be permissions or lack of support.
	# We return 2 to indicate "skip this event" rather than "fail test".
	if ! perf record -o "${perfdata}" -e "$event_name" $record_opts -- perf test -w thloop > /dev/null 2>&1; then
		echo "perf script perl test [Skipped: failed to record $event_name]"
		return 2
	fi

	echo "Generating perl script..."
	if ! perf script -i "${perfdata}" -g "${generated_script}"; then
		echo "perf script perl test [Failed: script generation for $event_name]"
		return 1
	fi

	if [ ! -f "${generated_script}" ]; then
		echo "perf script perl test [Failed: script not generated for $event_name]"
		return 1
	fi

	echo "Executing perl script..."
	output=$(perf script -i "${perfdata}" -s "${generated_script}" 2>&1)

	if echo "$output" | grep -q "$expected_output"; then
		echo "perf script perl test [Success: $event_name triggered $expected_output]"
		return 0
	else
		echo "perf script perl test [Failed: $event_name did not trigger $expected_output]"
		echo "Output was:"
		echo "$output" | head -n 20
		return 1
	fi
}

check_perl_support || exit 2

# Try tracepoint first
test_script "sched:sched_switch" "sched::sched_switch" "-c 1" && res=0 || res=$?

if [ $res -eq 0 ]; then
	exit 0
elif [ $res -eq 1 ]; then
	exit 1
fi

# If tracepoint skipped (res=2), try task-clock
# For generic events like task-clock, the generated script uses process_event()
# which dumps data using Data::Dumper. We check for "$VAR1" which is standard Dumper output.
test_script "task-clock" "\$VAR1" "-c 100" && res=0 || res=$?

if [ $res -eq 0 ]; then
	exit 0
elif [ $res -eq 1 ]; then
	exit 1
fi

# If both skipped
echo "perf script perl test [Skipped: Could not record tracepoint or task-clock]"
exit 2
