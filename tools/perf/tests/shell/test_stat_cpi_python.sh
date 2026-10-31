#!/bin/bash
# SPDX-License-Identifier: GPL-2.0
# stat-cpi python test (no_mlock)

set -e

shelldir=$(dirname "$0")
# shellcheck source=lib/setup_python.sh
. "${shelldir}"/lib/setup_python.sh

# If we don't have the perf python module, we can't test
if ! "$PYTHON" -c 'import perf' > /dev/null 2>&1; then
	echo "Skipping test, perf python module not found"
	return 2 2>/dev/null || exit 2
fi

script_dir="$(dirname "$0")/../../python"
script_path="${script_dir}/stat-cpi.py"

if [ ! -f "$script_path" ]; then
	echo "Skipping test, stat-cpi.py not found at $script_path"
	return 2 2>/dev/null || exit 2
fi

err=0
ran=0
temp_data=""
temp_out=""

cleanup() {
	[ -n "${pid}" ] && kill "$pid" 2>/dev/null || true
	[ -n "${workload_pid}" ] && kill "$workload_pid" 2>/dev/null || true
	rm -f "${temp_data}" "${temp_out}"
	trap - exit term int
}

trap_cleanup() {
	cleanup
	exit 1
}
trap trap_cleanup exit term int

temp_data=$(mktemp /tmp/perf.data.XXXXXX)
temp_out=$(mktemp /tmp/perf.out.XXXXXX)

test_live_mode() {
	echo "Testing stat-cpi.py live mode..."
	if ! perf stat -e cycles,instructions -- sleep 0.1 2>/dev/null && \
	   ! perf stat -e cycles:u,instructions:u -- sleep 0.1 2>/dev/null; then
		echo "perf stat failed (permissions?), skipping live mode test."
		return 0
	fi
	perf test -w noploop 60 &
	workload_pid=$!
	if ! perf stat -e cycles,instructions -p "$workload_pid" -- sleep 0.05 2>/dev/null && \
	   ! perf stat -e cycles:u,instructions:u -p "$workload_pid" -- sleep 0.05 2>/dev/null; then
		kill "$workload_pid" 2>/dev/null || true
		workload_pid=""
		echo "perf stat -p failed (ptrace_scope?), skipping live mode test."
		return 0
	fi
	ran=1

	# Run live mode in the background, wait until at least one interval is
	# printed, then interrupt.
	perf script stat-cpi -I 0.1 -p "$workload_pid" > "${temp_out}" &
	pid=$!
	for _ in $(seq 1 50); do
		if grep -q "cpi" "${temp_out}"; then
			break
		fi
		sleep 0.1
	done
	kill -INT "$pid" 2>/dev/null || true
	set +e
	wait "$pid"
	res=$?
	set -e
	pid=""
	kill "$workload_pid" 2>/dev/null || true
	workload_pid=""
	if [ $res -ne 0 ] && [ $res -ne 130 ] && [ $res -ne 143 ]; then
		echo "Live mode failed or crashed"
		err=1
	elif ! grep -q "cpi" "${temp_out}"; then
		echo "Live mode produced no cpi output"
		err=1
	else
		echo "Live mode test passed."
	fi
}

test_file_mode() {
	echo "Testing stat-cpi.py file mode..."
	# Generate some stat events - perf stat -I represents interval reporting
	if ! perf stat -e cycles,instructions -I 100 record -o "${temp_data}" \
		-- sleep 0.5 2>/dev/null && \
	   ! perf stat -e cycles:u,instructions:u -I 100 record -o "${temp_data}" \
		-- sleep 0.5 2>/dev/null; then
		echo "perf stat failed (permissions?), skipping file mode test."
		return
	fi
	ran=1

	out=$(perf script stat-cpi -i "${temp_data}")
	if ! echo "$out" | grep -q "cpi"; then
		echo "File mode test failed."
		err=1
	else
		echo "File mode test passed."
	fi
}

test_live_mode
test_file_mode

cleanup
if [ $ran -eq 0 ]; then
	exit 2
fi
exit $err
