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
jitdump_workload="${temp_dir}/jitdump_workload"
maxbrstack="${temp_dir}/maxbrstack.py"

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

# perf record for testing without decoding
perf_record_no_decode()
{
	# Options to speed up recording: no post-processing, no build-id cache update,
	# and no BPF events.
	perf record -B -N --no-bpf-event "$@"
}

# perf record for testing should not need BPF events
perf_record_no_bpf()
{
	# Options for no BPF events
	perf record --no-bpf-event "$@"
}

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
	if ! perf_record_no_decode -o "${tmpfile}" -e dummy:u -C "$1" true >/dev/null 2>&1 ; then
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
	perf_record_no_decode -o "${perfdatafile}" -e intel_pt//u -C 0 -- taskset --cpu-list 1 uname

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
	if [ -z "${can_kernel_trace}" ] ; then
		can_kernel_trace=0
		perf_record_no_decode -o "${tmpfile}" -e dummy:k true >/dev/null 2>&1 && can_kernel_trace=1
	fi
	if [ ${can_kernel_trace} -eq 0 ] ; then
		echo "SKIP: no kernel tracing"
		return 2
	fi
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

	perf_record_no_decode -o "${perfdatafile}" -e intel_pt//u"${k}" -vvv --per-thread -p "${w1},${w2}" 2>"${errfile}" >"${outfile}" &
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

test_jitdump()
{
	echo "--- Test tracing self-modifying code that uses jitdump ---"

	script_path=$(realpath "$0")
	script_dir=$(dirname "$script_path")
	jitdump_incl_dir="${script_dir}/../../util"
	jitdump_h="${jitdump_incl_dir}/jitdump.h"

	if [ ! -e "${jitdump_h}" ] ; then
		echo "SKIP: Include file jitdump.h not found"
		return 2
	fi

	if [ -z "${have_jitdump_workload}" ] ; then
		have_jitdump_workload=false
		# Create a workload that uses self-modifying code and generates its own jitdump file
		cat <<- "_end_of_file_" | /usr/bin/cc -o "${jitdump_workload}" -I "${jitdump_incl_dir}" -xc - -pthread && have_jitdump_workload=true
		#define _GNU_SOURCE
		#include <sys/mman.h>
		#include <sys/types.h>
		#include <stddef.h>
		#include <stdio.h>
		#include <stdint.h>
		#include <unistd.h>
		#include <string.h>

		#include "jitdump.h"

		#define CHK_BYTE 0x5a

		static inline uint64_t rdtsc(void)
		{
			unsigned int low, high;

			asm volatile("rdtsc" : "=a" (low), "=d" (high));

			return low | ((uint64_t)high) << 32;
		}

		static FILE *open_jitdump(void)
		{
			struct jitheader header = {
				.magic      = JITHEADER_MAGIC,
				.version    = JITHEADER_VERSION,
				.total_size = sizeof(header),
				.pid        = getpid(),
				.timestamp  = rdtsc(),
				.flags      = JITDUMP_FLAGS_ARCH_TIMESTAMP,
			};
			char filename[256];
			FILE *f;
			void *m;

			snprintf(filename, sizeof(filename), "jit-%d.dump", getpid());
			f = fopen(filename, "w+");
			if (!f)
				goto err;
			/* Create an MMAP event for the jitdump file. That is how perf tool finds it. */
			m = mmap(0, 4096, PROT_READ | PROT_EXEC, MAP_PRIVATE, fileno(f), 0);
			if (m == MAP_FAILED)
				goto err_close;
			munmap(m, 4096);
			if (fwrite(&header,sizeof(header),1,f) != 1)
				goto err_close;
			return f;

		err_close:
			fclose(f);
		err:
			return NULL;
		}

		static int write_jitdump(FILE *f, void *addr, const uint8_t *dat, size_t sz, uint64_t *idx)
		{
			struct jr_code_load rec = {
				.p.id          = JIT_CODE_LOAD,
				.p.total_size  = sizeof(rec) + sz,
				.p.timestamp   = rdtsc(),
				.pid	       = getpid(),
				.tid	       = gettid(),
				.vma           = (unsigned long)addr,
				.code_addr     = (unsigned long)addr,
				.code_size     = sz,
				.code_index    = ++*idx,
			};

			if (fwrite(&rec,sizeof(rec),1,f) != 1 ||
			fwrite(dat, sz, 1, f) != 1)
				return -1;
			return 0;
		}

		static void close_jitdump(FILE *f)
		{
			fclose(f);
		}

		int main()
		{
			/* Get a memory page to store executable code */
			void *addr = mmap(0, 4096, PROT_WRITE | PROT_EXEC, MAP_ANONYMOUS | MAP_PRIVATE, -1, 0);
			/* Code to execute: mov CHK_BYTE, %eax ; ret */
			uint8_t dat[] = {0xb8, CHK_BYTE, 0x00, 0x00, 0x00, 0xc3};
			FILE *f = open_jitdump();
			uint64_t idx = 0;
			int ret = 1;

			if (!f)
				return 1;
			/* Copy executable code to executable memory page */
			memcpy(addr, dat, sizeof(dat));
			/* Record it in the jitdump file */
			if (write_jitdump(f, addr, dat, sizeof(dat), &idx))
				goto out_close;
			/* Call it */
			ret = ((int (*)(void))addr)() - CHK_BYTE;
		out_close:
			close_jitdump(f);
			return ret;
		}
		_end_of_file_
	fi

	if ! $have_jitdump_workload ; then
		echo "SKIP: No jitdump workload"
		return 2
	fi

	# Change to temp_dir so jitdump collateral files go there
	cd "${temp_dir}"
	perf_record_no_bpf -o "${tmpfile}" -e intel_pt//u "${jitdump_workload}"
	perf inject -i "${tmpfile}" -o "${perfdatafile}" --jit
	decode_br_cnt=$(perf script -i "${perfdatafile}" --itrace=b | wc -l)
	# Note that overflow and lost errors are suppressed for the error count
	decode_err_cnt=$(perf script -i "${perfdatafile}" --itrace=e-o-l | grep -ci error)
	cd -
	# Should be thousands of branches
	if [ "${decode_br_cnt}" -lt 1000 ] ; then
		echo "Decode failed, only ${decode_br_cnt} branches"
		return 1
	fi
	# Should be no errors
	if [ "${decode_err_cnt}" -ne 0 ] ; then
		echo "Decode failed, ${decode_err_cnt} errors"
		perf script -i "${perfdatafile}" --itrace=e-o-l --show-mmap-events | cat
		return 1
	fi

	echo OK
	return 0
}

test_packet_filter()
{
	echo "--- Test with MTC and TSC disabled ---"
	# Disable MTC and TSC
	perf_record_no_decode -o "${perfdatafile}" -e intel_pt/mtc=0,tsc=0/u uname
	# Should not get MTC packet
	mtc_cnt=$(perf script -i "${perfdatafile}" -D 2>/dev/null | grep -c "MTC 0x")
	if [ "${mtc_cnt}" -ne 0 ] ; then
		echo "Failed to filter with mtc=0"
		return 1
	fi
	# Should not get TSC package
	tsc_cnt=$(perf script -i "${perfdatafile}" -D 2>/dev/null | grep -c "TSC 0x")
	if [ "${tsc_cnt}" -ne 0 ] ; then
		echo "Failed to filter with tsc=0"
		return 1
	fi
	echo OK
	return 0
}

test_disable_branch()
{
	echo "--- Test with branches disabled ---"
	# Disable branch
	perf_record_no_decode -o "${perfdatafile}" -e intel_pt/branch=0/u uname
	# Should not get branch related packets
	tnt_cnt=$(perf script -i "${perfdatafile}" -D 2>/dev/null | grep -c "TNT 0x")
	tip_cnt=$(perf script -i "${perfdatafile}" -D 2>/dev/null | grep -c "TIP 0x")
	fup_cnt=$(perf script -i "${perfdatafile}" -D 2>/dev/null | grep -c "FUP 0x")
	if [ "${tnt_cnt}" -ne 0 ] || [ "${tip_cnt}" -ne 0 ] || [ "${fup_cnt}" -ne 0 ] ; then
		echo "Failed to disable branches"
		return 1
	fi
	echo OK
	return 0
}

test_time_cyc()
{
	echo "--- Test with/without CYC ---"
	# Check if CYC is supported
	cyc=$(cat /sys/bus/event_source/devices/intel_pt/caps/psb_cyc)
	if [ "${cyc}" != "1" ] ; then
		echo "SKIP: CYC is not supported"
		return 2
	fi
	# Enable CYC
	perf_record_no_decode -o "${perfdatafile}" -e intel_pt/cyc/u uname
	# should get CYC packets
	cyc_cnt=$(perf script -i "${perfdatafile}" -D 2>/dev/null | grep -c "CYC 0x")
	if [ "${cyc_cnt}" = "0" ] ; then
		echo "Failed to get CYC packet"
		return 1
	fi
	# Without CYC
	perf_record_no_decode -o "${perfdatafile}" -e intel_pt//u uname
	# Should not get CYC packets
	cyc_cnt=$(perf script -i "${perfdatafile}" -D 2>/dev/null | grep -c "CYC 0x")
	if [ "${cyc_cnt}" -gt 0 ] ; then
		echo "Still get CYC packet without cyc"
		return 1
	fi
	echo OK
	return 0
}

test_sample()
{
	echo "--- Test recording with sample mode ---"
	# Check if recording with sample mode is working
	if ! perf_record_no_decode -o "${perfdatafile}" --aux-sample=8192 -e '{intel_pt//u,branch-misses:u}' uname ; then
		echo "perf record failed with --aux-sample"
		return 1
	fi
	# Check with event with PMU name
	if perf_record_no_decode -o "${perfdatafile}" -e br_misp_retired.all_branches:u uname ; then
		if ! perf_record_no_decode -o "${perfdatafile}" -e '{intel_pt//,br_misp_retired.all_branches/aux-sample-size=8192/}:u' uname ; then
			echo "perf record failed with --aux-sample-size"
			return 1
		fi
	fi
	echo OK
	return 0
}

test_kernel_trace()
{
	echo "--- Test with kernel trace ---"
	# Check if recording with kernel trace is working
	can_kernel || return 2
	if ! perf_record_no_decode -o "${perfdatafile}" -e intel_pt//k -m1,128 uname ; then
		echo "perf record failed with intel_pt//k"
		return 1
	fi
	echo OK
	return 0
}

test_virtual_lbr()
{
	echo "--- Test virtual LBR ---"
	# Check if python script is supported
	libpython=$(perf version --build-options | grep python | grep -cv OFF)
	if [ "${libpython}" != "1" ] ; then
		echo "SKIP: python scripting is not supported"
		return 2
	fi

	# Python script to determine the maximum size of branch stacks
	cat << "_end_of_file_" > "${maxbrstack}"
from __future__ import print_function

bmax = 0

def process_event(param_dict):
	if "brstack" in param_dict:
		brstack = param_dict["brstack"]
		n = len(brstack)
		global bmax
		if n > bmax:
			bmax = n

def trace_end():
	print("max brstack", bmax)
_end_of_file_

	# Check if virtual lbr is working
	perf_record_no_bpf -o "${perfdatafile}" --aux-sample -e '{intel_pt//,cycles}:u' uname
	times_val=$(perf script -i "${perfdatafile}" --itrace=L -s "${maxbrstack}" 2>/dev/null | grep "max brstack " | cut -d " " -f 3)
	case "${times_val}" in
		[0-9]*)	;;
		*)	times_val=0;;
	esac
	if [ "${times_val}" -lt 2 ] ; then
		echo "Failed with virtual lbr"
		return 1
	fi
	echo OK
	return 0
}

test_power_event()
{
	echo "--- Test power events ---"
	# Check if power events are supported
	power_event=$(cat /sys/bus/event_source/devices/intel_pt/caps/power_event_trace)
	if [ "${power_event}" != "1" ] ; then
		echo "SKIP: power_event_trace is not supported"
		return 2
	fi
	if ! perf_record_no_decode -o "${perfdatafile}" -a -e intel_pt/pwr_evt/u uname ; then
		echo "perf record failed with pwr_evt"
		return 1
	fi
	echo OK
	return 0
}

test_no_tnt()
{
	echo "--- Test with TNT packets disabled  ---"
	# Check if TNT disable is supported
	notnt=$(cat /sys/bus/event_source/devices/intel_pt/caps/tnt_disable)
	if [ "${notnt}" != "1" ] ; then
		echo "SKIP: tnt_disable is not supported"
		return 2
	fi
	perf_record_no_decode -o "${perfdatafile}" -e intel_pt/notnt/u uname
	# Should be no TNT packets
	tnt_cnt=$(perf script -i "${perfdatafile}" -D | grep -c TNT)
	if [ "${tnt_cnt}" -ne 0 ] ; then
		echo "TNT packets still there after notnt"
		return 1
	fi
	echo OK
	return 0
}

test_event_trace()
{
	echo "--- Test with event_trace ---"
	# Check if event_trace is supported
	event_trace=$(cat /sys/bus/event_source/devices/intel_pt/caps/event_trace)
	if [ "${event_trace}" != 1 ] ; then
		echo "SKIP: event_trace is not supported"
		return 2
	fi
	if ! perf_record_no_decode -o "${perfdatafile}" -e intel_pt/event/u uname ; then
		echo "perf record failed with event trace"
		return 1
	fi
	echo OK
	return 0
}

test_pipe()
{
	echo "--- Test with pipe mode ---"
	# Check if it works with pipe
	if ! perf_record_no_bpf -o- -e intel_pt//u uname | perf report -q -i- --itrace=i10000 ; then
		echo "perf record + report failed with pipe mode"
		return 1
	fi
	if ! perf_record_no_bpf -o- -e intel_pt//u uname | perf inject -b > /dev/null ; then
		echo "perf record + inject failed with pipe mode"
		return 1
	fi
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
test_system_wide_side_band		|| ret=$? ; count_result $ret ; ret=0
test_per_thread "" ""			|| ret=$? ; count_result $ret ; ret=0
test_per_thread "k" "(incl. kernel) "	|| ret=$? ; count_result $ret ; ret=0
test_jitdump				|| ret=$? ; count_result $ret ; ret=0
test_packet_filter			|| ret=$? ; count_result $ret ; ret=0
test_disable_branch			|| ret=$? ; count_result $ret ; ret=0
test_time_cyc				|| ret=$? ; count_result $ret ; ret=0
test_sample				|| ret=$? ; count_result $ret ; ret=0
test_kernel_trace			|| ret=$? ; count_result $ret ; ret=0
test_virtual_lbr			|| ret=$? ; count_result $ret ; ret=0
test_power_event			|| ret=$? ; count_result $ret ; ret=0
test_no_tnt				|| ret=$? ; count_result $ret ; ret=0
test_event_trace			|| ret=$? ; count_result $ret ; ret=0
test_pipe				|| ret=$? ; count_result $ret ; ret=0

cleanup

echo "--- Done ---"

if [ ${err_cnt} -gt 0 ] ; then
	exit 1
fi

if [ ${ok_cnt} -gt 0 ] ; then
	exit 0
fi

exit 2
