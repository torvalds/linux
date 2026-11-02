#!/bin/bash
# SPDX-License-Identifier: GPL-2.0
# rw-by-file python test

set -e

shelldir=$(dirname "$0")
# shellcheck source=lib/setup_python.sh
. "${shelldir}"/lib/setup_python.sh

if ! "$PYTHON" -c 'import perf' > /dev/null 2>&1; then
	echo "Skipping test, perf python module not found"
	exit 2
fi

script_dir="$(dirname "$0")/../../python"
script_path="${script_dir}/rw-by-file.py"

if ! perf check feature -q libtraceevent > /dev/null 2>&1; then
	echo "Skipping test, libtraceevent is disabled"
	exit 2
fi

if [ ! -f "$script_path" ]; then
	echo "Skipping test, rw-by-file.py not found at $script_path"
	exit 2
fi

err=0
cleanup() {
	rm -f "${temp_data}" "${temp_out}"
}
trap 'cleanup' EXIT TERM INT
temp_data=$(mktemp /tmp/perf.data.XXXXXX)
temp_out=$(mktemp /tmp/perf.out.XXXXXX)

echo "Testing rw-by-file.py..."

# Create a perf.data file. Try to get tracepoint data.
if perf list tracepoint | grep -q "syscalls:sys_enter_read"; then
	perf record -B -N --no-bpf-event -e syscalls:sys_enter_read,syscalls:sys_enter_write \
		-o "${temp_data}" \
		-- dd if=/dev/urandom of=/dev/null bs=1M count=10 >/dev/null 2>&1 || \
		{ echo "Skipping test, perf record failed"; exit 2; }
else
	echo "Skipping test, no syscalls:sys_enter_read event"
	exit 2
fi

if [ ! -s "${temp_data}" ]; then
	echo "Skipping test, perf record failed to create data"
	exit 2
fi

# Check that the script executes - filtering for "dd" since that's what we ran
if ! perf script rw-by-file -i "${temp_data}" "dd" > "${temp_out}"; then
	echo "rw-by-file.py test failed"
	err=1
else
	if ! grep -E -q "^ *[0-9]+" "${temp_out}"; then
		echo "Failed to find metric data rows"
		err=1
	else
		echo "rw-by-file test passed."
	fi
fi
rm -f "${temp_out}"

exit $err
