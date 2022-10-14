#!/bin/sh
# Miscellaneous Intel PT testing
# SPDX-License-Identifier: GPL-2.0

set -e

# Skip if no Intel PT
perf list | grep -q 'intel_pt//' || exit 2

shelldir=$(dirname "$0")
. "${shelldir}"/lib/waiting.sh

skip_cnt=0
ok_cnt=0
err_cnt=0

temp_dir=$(mktemp -d /tmp/perf-test-intel-pt-sh.XXXXXXXXXX)

tmpfile="${temp_dir}/tmp-perf.data"
perfdatafile="${temp_dir}/test-perf.data"
outfile="${temp_dir}/test-out.txt"
errfile="${temp_dir}/test-err.txt"
workload="${temp_dir}/workload"
awkscript="${temp_dir}/awkscript"

cleanup()
{
	trap - EXIT TERM INT
	sane=$(echo "${temp_dir}" | cut -b 1-26)
	if [ "${sane}" = "/tmp/perf-test-intel-pt-sh" ] ; then
		echo "--- Cleaning up ---"
		rm -f "${temp_dir}/"*
		rmdir "${temp_dir}"
	fi
}

trap_cleanup()
{
	cleanup
	exit 1
}

trap trap_cleanup EXIT TERM INT

have_workload=false
cat << _end_of_file_ | /usr/bin/cc -o "${workload}" -xc - -pthread && have_workload=true
#include <time.h>
#include <pthread.h>

void work(void) {
	struct timespec tm = {
		.tv_nsec = 1000000,
	};
	int i;

	/* Run for about 30 seconds */
	for (i = 0; i < 30000; i++)
		nanosleep(&tm, NULL);
}

void *threadfunc(void *arg) {
	work();
	return NULL;
}

int main(void) {
	pthread_t th;

	pthread_create(&th, NULL, threadfunc, NULL);
	work();
	pthread_join(th, NULL);
	return 0;
}
_end_of_file_

can_cpu_wide()
{
	echo "Checking for CPU-wide recording on CPU $1"
	if ! perf record -o "${tmpfile}" -B -N --no-bpf-event -e dummy:u -C "$1" true >/dev/null 2>&1 ; then
		echo "No so skipping"
		return 2
	fi
	echo OK
	return 0
}

test_system_wide_side_band()
{
	echo "--- Test system-wide sideband ---"

	# Need CPU 0 and CPU 1
	can_cpu_wide 0 || return $?
	can_cpu_wide 1 || return $?

	# Record on CPU 0 a task running on CPU 1
	perf record -B -N --no-bpf-event -o "${perfdatafile}" -e intel_pt//u -C 0 -- taskset --cpu-list 1 uname

	# Should get MMAP events from CPU 1 because they can be needed to decode
	mmap_cnt=$(perf script -i "${perfdatafile}" --no-itrace --show-mmap-events -C 1 2>/dev/null | grep -c MMAP)

	if [ "${mmap_cnt}" -gt 0 ] ; then
		echo OK
		return 0
	fi

	echo "Failed to record MMAP events on CPU 1 when tracing CPU 0"
	return 1
}

can_kernel()
{
	perf record -o "${tmpfile}" -B -N --no-bpf-event -e dummy:k true >/dev/null 2>&1 || return 2
	return 0
}

test_per_thread()
{
	k="$1"
	desc="$2"

	echo "--- Test per-thread ${desc}recording ---"

	if ! $have_workload ; then
		echo "No workload, so skipping"
		return 2
	fi

	if [ "${k}" = "k" ] ; then
		can_kernel || return 2
	fi

	cat <<- "_end_of_file_" > "${awkscript}"
	BEGIN {
		s = "[ ]*"
		u = s"[0-9]+"s
		d = s"[0-9-]+"s
		x = s"[0-9a-fA-FxX]+"s
		mmapping = "idx"u": mmapping fd"u
		set_output = "idx"u": set output fd"u"->"u
		perf_event_open = "sys_perf_event_open: pid"d"cpu"d"group_fd"d"flags"x"="u
	}

	/perf record opening and mmapping events/ {
		if (!done)
			active = 1
	}

	/perf record done opening and mmapping events/ {
		active = 0
		done = 1
	}

	$0 ~ perf_event_open && active {
		match($0, perf_event_open)
		$0 = substr($0, RSTART, RLENGTH)
		pid = $3
		cpu = $5
		fd = $11
		print "pid " pid " cpu " cpu " fd " fd " : " $0
		fd_array[fd] = fd
		pid_array[fd] = pid
		cpu_array[fd] = cpu
	}

	$0 ~ mmapping && active  {
		match($0, mmapping)
		$0 = substr($0, RSTART, RLENGTH)
		fd = $5
		print "fd " fd " : " $0
		if (fd in fd_array) {
			mmap_array[fd] = 1
		} else {
			print "Unknown fd " fd
			exit 1
		}
	}

	$0 ~ set_output && active {
		match($0, set_output)
		$0 = substr($0, RSTART, RLENGTH)
		fd = $6
		fd_to = $8
		print "fd " fd " fd_to " fd_to " : " $0
		if (fd in fd_array) {
			if (fd_to in fd_array) {
				set_output_array[fd] = fd_to
			} else {
				print "Unknown fd " fd_to
				exit 1
			}
		} else {
			print "Unknown fd " fd
			exit 1
		}
	}

	END {
		print "Checking " length(fd_array) " fds"
		for (fd in fd_array) {
			if (fd in mmap_array) {
				pid = pid_array[fd]
				if (pid != -1) {
					if (pid in pids) {
						print "More than 1 mmap for PID " pid
						exit 1
					}
					pids[pid] = 1
				}
				cpu = cpu_array[fd]
				if (cpu != -1) {
					if (cpu in cpus) {
						print "More than 1 mmap for CPU " cpu
						exit 1
					}
					cpus[cpu] = 1
				}
			} else if (!(fd in set_output_array)) {
				print "No mmap for fd " fd
				exit 1
			}
		}
		n = length(pids)
		if (n != thread_cnt) {
			print "Expected " thread_cnt " per-thread mmaps - found " n
			exit 1
		}
	}
	_end_of_file_

	$workload &
	w1=$!
	$workload &
	w2=$!
	echo "Workload PIDs are $w1 and $w2"
	wait_for_threads ${w1} 2
	wait_for_threads ${w2} 2

	perf record -B -N --no-bpf-event -o "${perfdatafile}" -e intel_pt//u"${k}" -vvv --per-thread -p "${w1},${w2}" 2>"${errfile}" >"${outfile}" &
	ppid=$!
	echo "perf PID is $ppid"
	wait_for_perf_to_start ${ppid} "${errfile}" || return 1

	kill ${w1}
	wait_for_process_to_exit ${w1} || return 1
	is_running ${ppid} || return 1

	kill ${w2}
	wait_for_process_to_exit ${w2} || return 1
	wait_for_process_to_exit ${ppid} || return 1

	awk -v thread_cnt=4 -f "${awkscript}" "${errfile}" || return 1

	echo OK
	return 0
}

count_result()
{
	if [ "$1" -eq 2 ] ; then
		skip_cnt=$((skip_cnt + 1))
		return
	fi
	if [ "$1" -eq 0 ] ; then
		ok_cnt=$((ok_cnt + 1))
		return
	fi
	err_cnt=$((err_cnt + 1))
}

ret=0
test_system_wide_side_band || ret=$? ; count_result $ret ; ret=0
test_per_thread "" "" || ret=$? ; count_result $ret ; ret=0
test_per_thread "k" "(incl. kernel) " || ret=$? ; count_result $ret ; ret=0

cleanup

echo "--- Done ---"

if [ ${err_cnt} -gt 0 ] ; then
	exit 1
fi

if [ ${ok_cnt} -gt 0 ] ; then
	exit 0
fi

exit 2
