#!/bin/bash
# perf top tests (exclusive)
# SPDX-License-Identifier: GPL-2.0

set -e

err=0
log_file=$(mktemp /tmp/perf.top.log.XXXXX)

cleanup() {
	rm -f "${log_file}"
	trap - EXIT TERM INT
}

trap_cleanup() {
	echo "Unexpected signal in ${FUNCNAME[1]}"
	cleanup
	exit 1
}
trap trap_cleanup EXIT TERM INT

test_basic_perf_top() {
	echo "Basic perf top test"

	# Start a workload that spins to generate samples
	# thloop runs for the specified number of seconds
	perf test -w thloop 20 &
	PID=$!

	# Allow it to start
	sleep 0.1

	# Run perf top for 5 seconds, monitoring that PID
	# Use --stdio to avoid TUI and redirect output
	# Use -d 1 to avoid flooding output
	# Use -e cpu-clock to ensure we get samples
	# Use sleep to keep stdin open but silent, preventing EOF loop or interactive spam
	retval=0
	sleep 10 | timeout 5s perf top --stdio -d 1 -e cpu-clock \
		-p $PID > "${log_file}" 2>&1 || retval=$?
	if [ "${retval:-0}" -ne 124 ] && [ "${retval:-0}" -ne 0 ]; then
		echo "Basic perf top test [Failed: perf top failed to start or run (ret=$retval)]"
		head -n 50 "${log_file}"
		kill $PID 2>/dev/null || true
		wait $PID 2>/dev/null || true
		err=1
		return
	fi

	kill $PID 2>/dev/null || true
	wait $PID 2>/dev/null || true


	# Check for some sample data (percentage)
	if ! grep -E -q "[0-9]+\.[0-9]+%" "${log_file}"; then
		echo "Basic perf top test [Failed: no sample percentage found]"
		head -n 50 "${log_file}"
		err=1
		return
	fi

	# Check for the symbol
	if ! grep -q "test_loop" "${log_file}"; then
		echo "Basic perf top test [Failed: test_loop symbol not found]"
		head -n 50 "${log_file}"
		err=1
		return
	fi

	echo "Basic perf top test [Success]"
}

test_hybrid_merge_perf_top() {
	echo "Perf top hybrid merge test"

	perf test -w thloop 20 &
	PID=$!

	# Allow it to start
	sleep 0.1

	# Run without explicitly requesting -e cycles so heavily virtualized
	# environments can seamlessly fall back to cpu-clock while real
	# hybrid hardware will naturally cover the merge logic.
	retval=0
	sleep 10 | timeout 5s perf top \
			--stdio --hybrid-merge -d 1 -p $PID > "${log_file}" 2>&1 || retval=$?
	if [ "${retval:-0}" -ne 124 ] && [ "${retval:-0}" -ne 0 ]; then
		echo "Perf top hybrid merge test [Failed: run err=$retval]"
		head -n 50 "${log_file}"
		kill $PID 2>/dev/null || true
		wait $PID 2>/dev/null || true
		err=1
		return
	fi

	kill $PID 2>/dev/null || true
	wait $PID 2>/dev/null || true

	# Wait a tiny bit for the file system to catch up on the logs
	sleep 0.1

	# Check for some sample data (percentage)
	if ! grep -E -q "[0-9]+\.[0-9]+%" "${log_file}"; then
		echo "Perf top hybrid merge test [Failed: no sample percentage found]"
		head -n 50 "${log_file}"
		err=1
		return
	fi

	# Check for the test loop symbol to ensure attribution worked
	if ! grep -q "test_loop" "${log_file}"; then
		echo "Perf top hybrid merge test [Failed: test_loop symbol not found]"
		head -n 50 "${log_file}"
		err=1
		return
	fi

	echo "Perf top hybrid merge test [Success]"
}

test_basic_perf_top
test_hybrid_merge_perf_top
cleanup
exit $err
