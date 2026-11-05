#!/bin/bash
# SPDX-License-Identifier: GPL-2.0
# syscall-counts-by-pid python test

set -e

shelldir=$(dirname "$0")
# shellcheck source=lib/setup_python.sh
. "${shelldir}"/lib/setup_python.sh

# If we don't have the perf python module, we can't test
if ! "$PYTHON" -c 'import perf' > /dev/null 2>&1; then
	echo "Skipping test, perf python module not found"
	exit 2
fi

script_dir="$(dirname "$0")/../../python"
script_path="${script_dir}/syscall-counts-by-pid.py"

if ! perf check feature -q libtraceevent > /dev/null 2>&1; then
	echo "Skipping test, libtraceevent is disabled"
	exit 2
fi

if [ ! -f "$script_path" ]; then
	echo "Skipping test, syscall-counts-by-pid.py not found at $script_path"
	exit 2
fi

err=0
temp_data=""
temp_out=""

cleanup() {
	rm -f "${temp_data}" "${temp_out}"
}

trap 'cleanup' EXIT TERM INT

temp_data=$(mktemp /tmp/perf.data.XXXXXX)
temp_out=$(mktemp /tmp/perf.out.XXXXXX)

test_file_mode() {
	echo "Testing syscall-counts-by-pid.py..."
	# Some systems might not have raw_syscalls:sys_enter
	if ! perf list tracepoint | grep -q raw_syscalls:sys_enter; then
		echo "Skipping test, raw_syscalls:sys_enter not found"
		exit 2
	fi

	# Generate some syscall events
	perf record -B -N --no-bpf-event -e raw_syscalls:sys_enter -o "${temp_data}" \
		-- sh -c "sleep 0.1; sleep 0.05" >/dev/null 2>&1 || \
		{ echo "Skipping test, perf record failed"; exit 2; }

	if ! perf script syscall-counts-by-pid -i "${temp_data}" > "${temp_out}"; then
		echo "File mode test failed."
		err=1
	elif ! grep -E -q "^  [a-zA-Z0-9_]+ +[0-9]+$" "${temp_out}"; then
		echo "File mode output validation failed."
		err=1
	else
		echo "File mode test passed."
	fi

	# Test with a comm argument
	if ! perf script syscall-counts-by-pid -i "${temp_data}" "sleep" > "${temp_out}"; then
		echo "Comm filter test failed."
		err=1
	elif ! grep -E -q "^  [a-zA-Z0-9_]+ +[0-9]+$" "${temp_out}"; then
		echo "Comm filter output validation failed."
		err=1
	else
		echo "Comm filter test passed."
	fi

	# Extract sleep PID from "sleep [<pid>]" header and test with a numeric PID filter argument
	sleep_pid=$(sed -n 's/^sleep \[\([0-9]\+\)\]$/\1/p' "${temp_out}" | head -n 1)
	if [ -z "${sleep_pid}" ] || \
	   ! perf script syscall-counts-by-pid -i "${temp_data}" "${sleep_pid}" > "${temp_out}"; then
		echo "PID filter test failed."
		err=1
	elif ! grep -E -q "^  [a-zA-Z0-9_]+ +[0-9]+$" "${temp_out}"; then
		echo "PID filter output validation failed."
		err=1
	else
		echo "PID filter test passed."
	fi
}

test_file_mode

exit $err
