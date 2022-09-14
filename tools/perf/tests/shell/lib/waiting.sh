# SPDX-License-Identifier: GPL-2.0

tenths=date\ +%s%1N

# Wait for PID $1 to have $2 number of threads started
wait_for_threads()
{
	start_time=$($tenths)
	while [ -e "/proc/$1/task" ] ; do
		th_cnt=$(find "/proc/$1/task" -mindepth 1 -maxdepth 1 -printf x | wc -c)
		if [ "${th_cnt}" -ge "$2" ] ; then
			return 0
		fi
		# Wait at most 5 seconds
		if [ $(($($tenths) - start_time)) -ge 50 ] ; then
			echo "PID $1 does not have $2 threads"
			return 1
		fi
	done
	return 1
}

# Wait for perf record -vvv 2>$2 with PID $1 to start by looking at file $2
# It depends on capturing perf record debug message "perf record has started"
wait_for_perf_to_start()
{
	echo "Waiting for \"perf record has started\" message"
	start_time=$($tenths)
	while [ -e "/proc/$1" ] ; do
		if grep -q "perf record has started" "$2" ; then
			echo OK
			break
		fi
		# Wait at most 5 seconds
		if [ $(($($tenths) - start_time)) -ge 50 ] ; then
			echo "perf recording did not start"
			return 1
		fi
	done
	return 0
}

# Wait for process PID %1 to exit
wait_for_process_to_exit()
{
	start_time=$($tenths)
	while [ -e "/proc/$1" ] ; do
		# Wait at most 5 seconds
		if [ $(($($tenths) - start_time)) -ge 50 ] ; then
			echo "PID $1 did not exit as expected"
			return 1
		fi
	done
	return 0
}

# Check if PID $1 is still running after 0.3 seconds
is_running()
{
	start_time=$($tenths)
	while [ -e "/proc/$1" ] ; do
		# Check for at least 0.3s
		if [ $(($($tenths) - start_time)) -gt 3 ] ; then
			return 0
		fi
	done
	echo "PID $1 exited prematurely"
	return 1
}
