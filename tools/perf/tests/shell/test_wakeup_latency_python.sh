#!/bin/bash
# SPDX-License-Identifier: GPL-2.0
# wakeup-latency python test

set -e

shelldir=$(dirname "$0")
# shellcheck source=lib/setup_python.sh
. "${shelldir}"/lib/setup_python.sh

if ! "$PYTHON" -c 'import perf' > /dev/null 2>&1; then
	echo "Skipping test, perf python module not found"
	exit 2
fi

if ! perf check feature -q libtraceevent; then
	echo "Skipping test, libtraceevent is disabled"
	exit 2
fi

script_dir="$(dirname "$0")/../../python"
script_path="${script_dir}/wakeup-latency.py"

if [ ! -f "$script_path" ]; then
	echo "Skipping test, wakeup-latency.py not found at $script_path"
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

echo "Testing wakeup-latency.py..."

# Create a perf.data file. Try to get tracepoint data.
if perf list tracepoint | grep -q "sched:sched_wakeup"; then
	ev="sched:sched_wakeup,sched:sched_wakeup_new,sched:sched_switch"
	perf record -B -N --no-bpf-event -e "$ev" -a -o "${temp_data}" \
		-- sleep 0.1 >/dev/null 2>&1 || \
		{ echo "Skipping test, perf record failed"; exit 2; }
else
	echo "Skipping test, no sched:sched_wakeup event"
	exit 2
fi

if [ ! -s "${temp_data}" ]; then
	echo "Skipping test, perf record failed to create data"
	exit 2
fi

# Check that the script executes
if ! perf script wakeup-latency -i "${temp_data}" > "${temp_out}"; then
	echo "wakeup-latency.py test failed"
	err=1
else
	if ! grep -E -q "avg_wakeup_latency.*[0-9]+" "${temp_out}"; then
		echo "Failed to find metric data rows"
		err=1
	else
		echo "wakeup-latency test passed."
	fi
fi

# Also test zero-wakeups / unhandled events path to verify division-by-zero protection
if perf record -B -N --no-bpf-event -e cycles -o "${temp_data}" \
	-- perf test -w noploop >/dev/null 2>&1; then
	if ! perf script wakeup-latency -i "${temp_data}" > "${temp_out}" || \
	   ! grep -q "avg_wakeup_latency (ns): N/A" "${temp_out}"; then
		echo "wakeup-latency zero-wakeups guard test failed"
		err=1
	fi
fi
rm -f "${temp_out}"

exit $err
