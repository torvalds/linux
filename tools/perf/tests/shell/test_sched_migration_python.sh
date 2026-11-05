#!/bin/bash
# SPDX-License-Identifier: GPL-2.0
# sched-migration python test

set -e

shelldir=$(dirname "$0")
# shellcheck source=lib/setup_python.sh
. "${shelldir}"/lib/setup_python.sh

if ! "$PYTHON" -c 'import perf' > /dev/null 2>&1; then
	echo "Skipping test, perf python module not found"
	exit 2
fi

script_dir="$(dirname "$0")/../../python"
script_path="${script_dir}/sched-migration.py"

if ! perf check feature -q libtraceevent > /dev/null 2>&1; then
	echo "Skipping test, libtraceevent is disabled"
	exit 2
fi

if [ ! -f "$script_path" ]; then
	echo "Skipping test, sched-migration.py not found at $script_path"
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

echo "Testing sched-migration.py..."

# Create a perf.data file. Force dropping a packet if tracepoint is available!
ev="sched:sched_switch,sched:sched_migrate_task"
ev="${ev},sched:sched_wakeup_new,sched:sched_wakeup"
has_sched=1
if ! perf record -B -N --no-bpf-event -e "$ev" -a -o "${temp_data}" \
	-- sleep 0.1 >/dev/null 2>&1; then
	has_sched=0
	perf record -B -N --no-bpf-event -e cycles -o "${temp_data}" \
		-- perf test -w noploop >/dev/null 2>&1 || \
		{ echo "Skipping test, perf record failed"; exit 2; }
fi

if [ ! -s "${temp_data}" ]; then
	echo "Skipping test, perf record failed to create data"
	exit 2
fi

# Check that the script executes
if ! perf script sched-migration -v --no-gui -i "${temp_data}" > "${temp_out}"; then
	echo "sched-migration.py test failed"
	err=1
elif [ "$has_sched" -eq 1 ] && ! grep -q "Timeslices:" "${temp_out}"; then
	echo "sched-migration.py missing Timeslices output"
	err=1
else
	echo "sched-migration test passed."
fi
rm -f "${temp_out}"

exit $err
