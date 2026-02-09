#!/bin/bash
# perf script python tests
# SPDX-License-Identifier: GPL-2.0

set -e

# set PERF_EXEC_PATH to find scripts in the source directory
perfdir=$(dirname "$0")/../..
if [ -e "$perfdir/scripts/python/Perf-Trace-Util" ]; then
  export PERF_EXEC_PATH=$perfdir
fi


perfdata=$(mktemp /tmp/__perf_test_script_python.perf.data.XXXXX)
generated_script=$(mktemp /tmp/__perf_test_script.XXXXX.py)

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

check_python_support() {
	if perf check feature -q libpython; then
		return 0
	fi
	echo "perf script python test [Skipped: no libpython support]"
	return 2
}

test_script() {
	local event_name=$1
	local expected_output=$2
	local record_opts=$3

	echo "Testing event: $event_name"

	# Try to record. If this fails, it might be permissions or lack of
	# support. Return 2 to indicate "skip this event" rather than "fail
	# test".
	if ! perf record -o "${perfdata}" -e "$event_name" $record_opts -- perf test -w thloop > /dev/null 2>&1; then
		echo "perf script python test [Skipped: failed to record $event_name]"
		return 2
	fi

	echo "Generating python script..."
	if ! perf script -i "${perfdata}" -g "${generated_script}"; then
		echo "perf script python test [Failed: script generation for $event_name]"
		return 1
	fi

	if [ ! -f "${generated_script}" ]; then
		echo "perf script python test [Failed: script not generated for $event_name]"
		return 1
	fi

	# Perf script -g python doesn't generate process_event for generic
	# events so append it manually to test that the callback works.
	if ! grep -q "def process_event" "${generated_script}"; then
		cat <<EOF >> "${generated_script}"

def process_event(param_dict):
	print("param_dict: %s" % param_dict)
EOF
	fi

	echo "Executing python script..."
	output=$(perf script -i "${perfdata}" -s "${generated_script}" 2>&1)

	if echo "$output" | grep -q "$expected_output"; then
		echo "perf script python test [Success: $event_name triggered $expected_output]"
		return 0
	else
		echo "perf script python test [Failed: $event_name did not trigger $expected_output]"
		echo "Output was:"
		echo "$output" | head -n 20
		return 1
	fi
}

check_python_support || exit 2

# Try tracepoint first
test_script "sched:sched_switch" "sched__sched_switch" "-c 1" && res=0 || res=$?

if [ $res -eq 0 ]; then
	exit 0
elif [ $res -eq 1 ]; then
	exit 1
fi

# If tracepoint skipped (res=2), try task-clock
# For generic events like task-clock, the generated script uses process_event()
# which prints the param_dict.
test_script "task-clock" "param_dict" "-c 100" && res=0 || res=$?

if [ $res -eq 0 ]; then
	exit 0
elif [ $res -eq 1 ]; then
	exit 1
fi

# If both skipped
echo "perf script python test [Skipped: Could not record tracepoint or task-clock]"
exit 2
