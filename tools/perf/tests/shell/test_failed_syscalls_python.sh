#!/bin/bash
# SPDX-License-Identifier: GPL-2.0
# failed-syscalls python test

set -e

shelldir=$(dirname "$0")
# shellcheck source=lib/setup_python.sh
. "${shelldir}"/lib/setup_python.sh

if ! "$PYTHON" -c 'import perf' > /dev/null 2>&1; then
	echo "Skipping test, perf python module not found"
	exit 2
fi

script_dir="$(dirname "$0")/../../python"
script_path="${script_dir}/failed-syscalls.py"

if ! perf check feature -q libtraceevent > /dev/null 2>&1; then
	echo "Skipping test, libtraceevent is disabled"
	exit 2
fi

if [ ! -f "$script_path" ]; then
	echo "Skipping test, failed-syscalls.py not found at $script_path"
	exit 2
fi

err=0
temp_dir=$(mktemp -d /tmp/perf-failed-syscalls-XXXXXX)
temp_data="${temp_dir}/perf.data"
temp_out="${temp_dir}/perf.out"

cleanup() {
	rm -rf "${temp_dir}"
}
trap 'cleanup' EXIT TERM INT

echo "Testing failed-syscalls.py..."

# Check if sys_exit event can be recorded
if ! perf record -B -N --no-bpf-event -e raw_syscalls:sys_exit \
	-o /dev/null -- true >/dev/null 2>&1; then
	if ! perf record -B -N --no-bpf-event -e syscalls:sys_exit \
		-o /dev/null -- true >/dev/null 2>&1; then
		echo "Skipping test, no permission or support for sys_exit event"
		exit 2
	else
		EVENT="syscalls:sys_exit"
	fi
else
	EVENT="raw_syscalls:sys_exit"
fi

# Run perf record with a command that fails a syscall (ls non-existent file),
# sleeping briefly in the subshell so ls's PERF_RECORD_COMM and sys_exit events are flushed.
passed=0
for _ in 1 2 3 4 5; do
	rm -f "${temp_data}" "${temp_out}"
	perf record -B -N --no-bpf-event -e "${EVENT}" -o "${temp_data}" \
		-- sh -c "ls /nonexistent_file_for_test 2>/dev/null; sleep 0.05 || true" \
		>/dev/null 2>&1 || true

	if [ ! -s "${temp_data}" ]; then
		continue
	fi

	# Check that the script executes
	if perf script failed-syscalls -i "${temp_data}" > "${temp_out}" && \
	   grep -q "failed syscalls by comm" "${temp_out}" && \
	   grep -Eq '^ls[[:space:]]+[0-9]+' "${temp_out}"; then
		passed=1
		break
	fi
done

if [ ! -s "${temp_data}" ]; then
	echo "Skipping test, perf record failed to create data"
	exit 2
fi

if [ "$passed" -eq 0 ]; then
	echo "Failed to find the metrics table header or expected error"
	err=1
else
	echo "failed-syscalls test passed."
fi
rm -f "${temp_out}"

exit $err
