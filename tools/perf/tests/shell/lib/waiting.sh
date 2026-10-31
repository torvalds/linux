#!/bin/bash
# SPDX-License-Identifier: GPL-2.0

# Wait for PID $1 to have $2 number of threads started
# Time out after approx. $3 tenths of a second or 5 seconds if $3 is ""
wait_for_threads()
{
	tm_out=$3 ; [ -n "${tm_out}" ] || tm_out=50
	while [ -e "/proc/$1/task" ] ; do
		th_cnt=$(find "/proc/$1/task" -mindepth 1 -maxdepth 1 -printf x | wc -c)
		if [ "${th_cnt}" -ge "$2" ] ; then
			return 0
		fi
		if [ "${tm_out}" -le 0 ] ; then
			echo "PID $1 does not have $2 threads"
			return 1
		fi
		sleep 0.1
		tm_out=$((tm_out - 1))
	done
	return 1
}

# Wait for perf record -vvv 2>$2 with PID $1 to start by looking at file $2
# It depends on capturing perf record debug message "perf record has started"
# Time out after approx. $3 tenths of a second or 5 seconds if $3 is ""
wait_for_perf_to_start()
{
	tm_out=$3 ; [ -n "${tm_out}" ] || tm_out=50
	echo "Waiting for \"perf record has started\" message"
	while [ -e "/proc/$1" ] ; do
		if grep -q "perf record has started" "$2" ; then
			echo OK
			break
		fi
		if [ "${tm_out}" -le 0 ] ; then
			echo "perf recording did not start"
			return 1
		fi
		sleep 0.1
		tm_out=$((tm_out - 1))
	done
	return 0
}

# Wait for process PID %1 to exit
# Time out after approx. $2 tenths of a second or 5 seconds if $2 is ""
wait_for_process_to_exit()
{
	tm_out=$2 ; [ -n "${tm_out}" ] || tm_out=50
	while [ -e "/proc/$1" ] ; do
		if [ "${tm_out}" -le 0 ] ; then
			echo "PID $1 did not exit as expected"
			return 1
		fi
		sleep 0.1
		tm_out=$((tm_out - 1))
	done
	return 0
}

# Check if PID $1 is still running after approx. $2 tenths of a second
# or 0.3 seconds if $2 is ""
is_running()
{
	tm_out=$2 ; [ -n "${tm_out}" ] || tm_out=3
	while [ -e "/proc/$1" ] ; do
		if [ "${tm_out}" -le 0 ] ; then
			return 0
		fi
		sleep 0.1
		tm_out=$((tm_out - 1))
	done
	echo "PID $1 exited prematurely"
	return 1
}
