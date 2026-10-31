#!/bin/bash
# SPDX-License-Identifier: GPL-2.0
# failed-syscalls-by-pid python test

set -e

shelldir=$(dirname "$0")
# shellcheck source=lib/setup_python.sh
. "${shelldir}"/lib/setup_python.sh

if ! "$PYTHON" -c 'import perf' > /dev/null 2>&1; then
	echo "Skipping test, perf python module not found"
	exit 2
fi

script_dir="$(dirname "$0")/../../python"
script_path="${script_dir}/failed-syscalls-by-pid.py"

if ! perf check feature -q libtraceevent > /dev/null 2>&1; then
	echo "Skipping test, libtraceevent is disabled"
	exit 2
fi

if [ ! -f "$script_path" ]; then
	echo "Skipping test, failed-syscalls-by-pid.py not found at $script_path"
	exit 2
fi

err=0
temp_dir=$(mktemp -d /tmp/perf-failed-syscalls-by-pid-XXXXXX)
temp_data="${temp_dir}/perf.data"
temp_out="${temp_dir}/perf.out"

cleanup() {
	rm -rf "${temp_dir}"
}

trap 'cleanup' EXIT TERM INT

test_file_mode() {
	echo "Testing failed-syscalls-by-pid.py..."

	# Check if syscalls:sys_exit is supported/readable
	if ! perf record -B -N --no-bpf-event -e syscalls:sys_exit \
		-o /dev/null -- true >/dev/null 2>&1; then
		if ! perf record -B -N --no-bpf-event -e raw_syscalls:sys_exit \
			-o /dev/null -- true >/dev/null 2>&1; then
			echo "Skipping test, no syscalls:sys_exit or raw_syscalls:sys_exit event"
			exit 2
		else
			EVENT="raw_syscalls:sys_exit"
		fi
	else
		EVENT="syscalls:sys_exit"
	fi

	# Generate some events by running a command that fails a syscall
	# (e.g. failing stat on non-existent file), sleeping briefly in the
	# subshell so ls's PERF_RECORD_COMM and sys_exit events are flushed.
	passed=0
	for _ in 1 2 3 4 5; do
		rm -f "${temp_data}" "${temp_out}" "${temp_out}.comm" "${temp_out}.pid"
		perf record -B -N --no-bpf-event -e "${EVENT}" -o "${temp_data}" \
			-- sh -c "ls /does_not_exist 2>/dev/null; sleep 0.05 || true" \
			>/dev/null 2>&1 || true
		if [ ! -s "${temp_data}" ]; then
			continue
		fi

		if perf script failed-syscalls-by-pid -i "${temp_data}" > "${temp_out}" && \
		   grep -n -q "err = ENOENT" "${temp_out}" && \
		   perf script failed-syscalls-by-pid -i "${temp_data}" \
			"ls" > "${temp_out}.comm" && \
		   grep -q "err = ENOENT" "${temp_out}.comm"; then
			ls_pid=$(sed -n 's/^ls \[\([0-9][0-9]*\)\].*/\1/p' \
				"${temp_out}.comm" | head -n 1)
			if [ -n "${ls_pid}" ] && \
			   perf script failed-syscalls-by-pid -i "${temp_data}" \
				"${ls_pid}" > "${temp_out}.pid" && \
			   grep -q "err = ENOENT" "${temp_out}.pid"; then
				passed=1
				break
			fi
		fi
	done

	if [ ! -s "${temp_data}" ]; then
		echo "Skipping test, perf record failed to create data"
		exit 2
	fi

	if [ "$passed" -eq 0 ]; then
		echo "failed-syscalls-by-pid test failed."
		cat "${temp_out}" 2>/dev/null || true
		cat "${temp_out}.comm" 2>/dev/null || true
		cat "${temp_out}.pid" 2>/dev/null || true
		err=1
	else
		echo "failed-syscalls-by-pid test passed."
	fi
	rm -f "${temp_out}" "${temp_out}.comm" "${temp_out}.pid"
}

test_file_mode

exit $err
