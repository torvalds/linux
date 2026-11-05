#!/bin/bash
# SPDX-License-Identifier: GPL-2.0
# rwtop python test

set -e

shelldir=$(dirname "$0")
# shellcheck source=lib/setup_python.sh
. "${shelldir}"/lib/setup_python.sh

if ! "$PYTHON" -c 'import perf' > /dev/null 2>&1; then
	echo "Skipping test, perf python module not found"
	exit 2
fi

script_dir="$(dirname "$0")/../../python"
script_path="${script_dir}/rwtop.py"

if ! perf check feature -q libtraceevent > /dev/null 2>&1; then
	echo "Skipping test, libtraceevent is disabled"
	exit 2
fi

if [ ! -f "$script_path" ]; then
	echo "Skipping test, rwtop.py not found at $script_path"
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

echo "Testing rwtop.py..."

# Create a perf.data file. Try to get tracepoint data.
if perf list tracepoint | grep -q "syscalls:sys_enter_read"; then
	ev="syscalls:sys_enter_read,syscalls:sys_exit_read"
	ev="${ev},syscalls:sys_enter_write,syscalls:sys_exit_write"
	perf record -B -N --no-bpf-event -e "$ev" -o "${temp_data}" \
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

# Check that the script executes
if ! perf script rwtop -i "${temp_data}" > "${temp_out}"; then
	echo "rwtop.py test failed"
	err=1
else
	if ! grep -E -q "^ *[0-9]+" "${temp_out}"; then
		echo "Failed to find metric data rows"
		err=1
	else
		echo "rwtop test passed."
	fi
fi
rm -f "${temp_out}"

exit $err
