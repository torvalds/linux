#!/bin/bash
# SPDX-License-Identifier: GPL-2.0
# event_analyzing_sample python test

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
script_path="${script_dir}/event_analyzing_sample.py"

if [ ! -f "$script_path" ]; then
	echo "Skipping test, event_analyzing_sample.py not found at $script_path"
	exit 2
fi

err=0
temp_dir=""

cleanup() {
	rm -rf "${temp_dir}"
}

trap 'cleanup' EXIT TERM INT

temp_dir=$(mktemp -d /tmp/perf.event_analyzing.XXXXXX)
temp_data="${temp_dir}/perf.data"
temp_db="${temp_dir}/perf.db"

test_file_mode() {
	echo "Testing event_analyzing_sample.py..."

	# Generate some events
	if ! perf record -B -N --no-bpf-event -o "${temp_data}" \
		-- perf test -w noploop >/dev/null 2>&1; then
		echo "Skipping test, perf record failed"
		exit 2
	fi

	# Run the script with default (:memory:) database and with explicit -d path
	if ! perf script event_analyzing_sample -i "${temp_data}" \
		> "${temp_dir}/perf.mem.out" 2>&1 || \
	   ! grep -q "Statistics about the general events" "${temp_dir}/perf.mem.out" || \
	   grep -q "Error creating/inserting event" "${temp_dir}/perf.mem.out"; then
		echo "Default in-memory database mode test failed."
		err=1
	elif ! perf script event_analyzing_sample -i "${temp_data}" \
		-d "${temp_db}" > "${temp_dir}/perf.out" 2>&1; then
		echo "File mode test failed."
		err=1
	elif ! grep -q "Statistics about the general events" "${temp_dir}/perf.out" || \
	     grep -q "Error creating/inserting event" "${temp_dir}/perf.out"; then
		echo "Event analysis output validation failed."
		err=1
	else
		echo "File mode test passed."
	fi
}

test_file_mode

exit $err
