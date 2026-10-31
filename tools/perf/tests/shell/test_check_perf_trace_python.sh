#!/bin/bash
# SPDX-License-Identifier: GPL-2.0
# check-perf-trace python test

set -e

shelldir=$(dirname "$0")
# shellcheck source=lib/setup_python.sh
. "${shelldir}"/lib/setup_python.sh

# If we don't have the perf python module, we can't test
if ! "$PYTHON" -c 'import perf' > /dev/null 2>&1; then
	echo "Skipping test, perf python module not found"
	exit 2
fi

if ! perf check feature -q libtraceevent; then
	echo "Skipping test, perf built without libtraceevent"
	exit 2
fi

script_dir="$(dirname "$0")/../../python"
script_path="${script_dir}/check-perf-trace.py"

if [ ! -f "$script_path" ]; then
	echo "Skipping test, check-perf-trace.py not found at $script_path"
	exit 2
fi

err=0
temp_data=""
temp_out=""

cleanup() {
	rm -f "${temp_data}" "${temp_out}"
}

trap 'cleanup' EXIT
trap 'cleanup; exit 1' TERM INT

temp_data=$(mktemp /tmp/perf.data.XXXXXX)
temp_out=$(mktemp /tmp/perf.out.XXXXXX)

test_file_mode() {
	echo "Testing check-perf-trace.py..."

	events=""
	tp_list=$(perf list tracepoint)
	if echo "$tp_list" | grep -q "irq:softirq_entry"; then
		events="irq:softirq_entry"
	fi
	if echo "$tp_list" | grep -q "kmem:kmalloc"; then
		if [ -n "$events" ]; then
			events="$events,kmem:kmalloc,kmem:kfree"
		else
			events="kmem:kmalloc,kmem:kfree"
		fi
	fi

	if [ -z "$events" ]; then
		echo "Skipping test, no required tracepoints found"
		exit 2
	fi

	# Generate events
	if ! perf record -B -N --no-bpf-event -e "$events" -o "${temp_data}" \
		-- sh -c "sleep 0.1" >/dev/null 2>&1; then
		echo "Skipping test, perf record failed"
		exit 2
	fi

	# Run the script
	if ! perf script check-perf-trace -i "${temp_data}" > "${temp_out}"; then
		echo "File mode test failed."
		err=1
	elif ! grep -q "in trace_begin" "${temp_out}" || \
	     ! grep -q "in trace_end" "${temp_out}"; then
		echo "File mode test missing trace_begin/trace_end markers."
		err=1
	else
		echo "File mode test passed."
	fi
}

test_file_mode

exit $err
