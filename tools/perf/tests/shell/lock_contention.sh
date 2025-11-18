#!/bin/bash
# kernel lock contention analysis test
# SPDX-License-Identifier: GPL-2.0

set -e

err=0
perfdata=$(mktemp /tmp/__perf_test.perf.data.XXXXX)
result=$(mktemp /tmp/__perf_test.result.XXXXX)
errout=$(mktemp /tmp/__perf_test.errout.XXXXX)

cleanup() {
	rm -f ${perfdata}
	rm -f ${result}
	rm -f ${errout}
	trap - EXIT TERM INT ERR
}

trap_cleanup() {
	if (( $? == 139 )); then #SIGSEGV
		err=1
	fi
	echo "Unexpected signal in ${FUNCNAME[1]}"
	cleanup
	exit ${err}
}
trap trap_cleanup EXIT TERM INT ERR

check() {
	if [ "$(id -u)" != 0 ]; then
		echo "[Skip] No root permission"
		err=2
		exit
	fi

	if ! perf list tracepoint | grep -q lock:contention_begin; then
		echo "[Skip] No lock contention tracepoints"
		err=2
		exit
	fi

	# shellcheck disable=SC2046
	if [ `nproc` -lt 4 ]; then
		echo "[Skip] Low number of CPUs (`nproc`), lock event cannot be triggered certainly"
		err=2
		exit
	fi
}

test_record()
{
	echo "Testing perf lock record and perf lock contention"
	perf lock record -o ${perfdata} -- perf bench sched messaging -p > /dev/null 2>&1
	# the output goes to the stderr and we expect only 1 output (-E 1)
	perf lock contention -i ${perfdata} -E 1 -q 2> ${result}
	if [ "$(cat "${result}" | wc -l)" != "1" ]; then
		echo "[Fail] Recorded result count is not 1:" "$(cat "${result}" | wc -l)"
		err=1
		exit
	fi
}

test_bpf()
{
	echo "Testing perf lock contention --use-bpf"

	if ! perf lock con -b true > /dev/null 2>&1 ; then
		echo "[Skip] No BPF support"
		return
	fi

	# the perf lock contention output goes to the stderr
	perf lock con -a -b -E 1 -q -- perf bench sched messaging -p > /dev/null 2> ${result}
	if [ "$(cat "${result}" | wc -l)" != "1" ]; then
		echo "[Fail] BPF result count is not 1:" "$(cat "${result}" | wc -l)"
		err=1
		exit
	fi
}

test_record_concurrent()
{
	echo "Testing perf lock record and perf lock contention at the same time"
	perf lock record -o- -- perf bench sched messaging -p 2> ${errout} | \
	perf lock contention -i- -E 1 -q 2> ${result}
	if [ "$(cat "${result}" | wc -l)" != "1" ]; then
		echo "[Fail] Recorded result count is not 1:" "$(cat "${result}" | wc -l)"
		cat ${errout}
		cat ${result}
		err=1
		exit
	fi
}

test_aggr_task()
{
	echo "Testing perf lock contention --threads"
	perf lock contention -i ${perfdata} -t -E 1 -q 2> ${result}
	if [ "$(cat "${result}" | wc -l)" != "1" ]; then
		echo "[Fail] Recorded result count is not 1:" "$(cat "${result}" | wc -l)"
		err=1
		exit
	fi

	if ! perf lock con -b true > /dev/null 2>&1 ; then
		return
	fi

	# the perf lock contention output goes to the stderr
	perf lock con -a -b -t -E 1 -q -- perf bench sched messaging -p > /dev/null 2> ${result}
	if [ "$(cat "${result}" | wc -l)" != "1" ]; then
		echo "[Fail] BPF result count is not 1:" "$(cat "${result}" | wc -l)"
		err=1
		exit
	fi
}

test_aggr_addr()
{
	echo "Testing perf lock contention --lock-addr"
	perf lock contention -i ${perfdata} -l -E 1 -q 2> ${result}
	if [ "$(cat "${result}" | wc -l)" != "1" ]; then
		echo "[Fail] Recorded result count is not 1:" "$(cat "${result}" | wc -l)"
		err=1
		exit
	fi

	if ! perf lock con -b true > /dev/null 2>&1 ; then
		return
	fi

	# the perf lock contention output goes to the stderr
	perf lock con -a -b -l -E 1 -q -- perf bench sched messaging -p > /dev/null 2> ${result}
	if [ "$(cat "${result}" | wc -l)" != "1" ]; then
		echo "[Fail] BPF result count is not 1:" "$(cat "${result}" | wc -l)"
		err=1
		exit
	fi
}

test_aggr_cgroup()
{
	echo "Testing perf lock contention --lock-cgroup"

	if ! perf lock con -b true > /dev/null 2>&1 ; then
		echo "[Skip] No BPF support"
		return
	fi

	# the perf lock contention output goes to the stderr
	perf lock con -a -b --lock-cgroup -E 1 -q -- perf bench sched messaging -p > /dev/null 2> ${result}
	if [ "$(cat "${result}" | wc -l)" != "1" ]; then
		echo "[Fail] BPF result count is not 1:" "$(cat "${result}" | wc -l)"
		err=1
		exit
	fi
}

test_type_filter()
{
	echo "Testing perf lock contention --type-filter (w/ spinlock)"
	perf lock contention -i ${perfdata} -Y spinlock -q 2> ${result}
	if [ "$(grep -c -v spinlock "${result}")" != "0" ]; then
		echo "[Fail] Recorded result should not have non-spinlocks:" "$(cat "${result}")"
		err=1
		exit
	fi

	if ! perf lock con -b true > /dev/null 2>&1 ; then
		return
	fi

	perf lock con -a -b -Y spinlock -q -- perf bench sched messaging -p > /dev/null 2> ${result}
	if [ "$(grep -c -v spinlock "${result}")" != "0" ]; then
		echo "[Fail] BPF result should not have non-spinlocks:" "$(cat "${result}")"
		err=1
		exit
	fi
}

test_lock_filter()
{
	echo "Testing perf lock contention --lock-filter (w/ tasklist_lock)"
	perf lock contention -i ${perfdata} -l -q 2> ${result}
	if [ "$(grep -c tasklist_lock "${result}")" != "1" ]; then
		echo "[Skip] Could not find 'tasklist_lock'"
		return
	fi

	perf lock contention -i ${perfdata} -L tasklist_lock -q 2> ${result}

	# find out the type of tasklist_lock
	test_lock_filter_type=$(head -1 "${result}" | awk '{ print $8 }' | sed -e 's/:.*//')

	if [ "$(grep -c -v "${test_lock_filter_type}" "${result}")" != "0" ]; then
		echo "[Fail] Recorded result should not have non-${test_lock_filter_type} locks:" "$(cat "${result}")"
		err=1
		exit
	fi

	if ! perf lock con -b true > /dev/null 2>&1 ; then
		return
	fi

	perf lock con -a -b -L tasklist_lock -q -- perf bench sched messaging -p > /dev/null 2> ${result}
	if [ "$(grep -c -v "${test_lock_filter_type}" "${result}")" != "0" ]; then
		echo "[Fail] BPF result should not have non-${test_lock_filter_type} locks:" "$(cat "${result}")"
		err=1
		exit
	fi
}

test_stack_filter()
{
	echo "Testing perf lock contention --callstack-filter (w/ unix_stream)"
	perf lock contention -i ${perfdata} -v -q 2> ${result}
	if [ "$(grep -c unix_stream "${result}")" = "0" ]; then
		echo "[Skip] Could not find 'unix_stream'"
		return
	fi

	perf lock contention -i ${perfdata} -E 1 -S unix_stream -q 2> ${result}
	if [ "$(cat "${result}" | wc -l)" != "1" ]; then
		echo "[Fail] Recorded result should have a lock from unix_stream:" "$(cat "${result}")"
		err=1
		exit
	fi

	if ! perf lock con -b true > /dev/null 2>&1 ; then
		return
	fi

	perf lock con -a -b -S unix_stream -E 1 -q -- perf bench sched messaging -p > /dev/null 2> ${result}
	if [ "$(cat "${result}" | wc -l)" != "1" ]; then
		echo "[Fail] BPF result should have a lock from unix_stream:" "$(cat "${result}")"
		err=1
		exit
	fi
}

test_aggr_task_stack_filter()
{
	echo "Testing perf lock contention --callstack-filter with task aggregation"
	perf lock contention -i ${perfdata} -v -q 2> ${result}
	if [ "$(grep -c unix_stream "${result}")" = "0" ]; then
		echo "[Skip] Could not find 'unix_stream'"
		return
	fi

	perf lock contention -i ${perfdata} -t -E 1 -S unix_stream -q 2> ${result}
	if [ "$(cat "${result}" | wc -l)" != "1" ]; then
		echo "[Fail] Recorded result should have a task from unix_stream:" "$(cat "${result}")"
		err=1
		exit
	fi

	if ! perf lock con -b true > /dev/null 2>&1 ; then
		return
	fi

	perf lock con -a -b -t -S unix_stream -E 1 -q -- perf bench sched messaging -p > /dev/null 2> ${result}
	if [ "$(cat "${result}" | wc -l)" != "1" ]; then
		echo "[Fail] BPF result should have a task from unix_stream:" "$(cat "${result}")"
		err=1
		exit
	fi
}
test_cgroup_filter()
{
	echo "Testing perf lock contention --cgroup-filter"

	if ! perf lock con -b true > /dev/null 2>&1 ; then
		echo "[Skip] No BPF support"
		return
	fi

	perf lock con -a -b --lock-cgroup -E 1 -F wait_total -q -- perf bench sched messaging -p > /dev/null 2> ${result}
	if [ "$(cat "${result}" | wc -l)" != "1" ]; then
		echo "[Fail] BPF result should have a cgroup result:" "$(cat "${result}")"
		err=1
		exit
	fi

	cgroup=$(cat "${result}" | awk '{ print $3 }')
	perf lock con -a -b --lock-cgroup -E 1 -G "${cgroup}" -q -- perf bench sched messaging -p > /dev/null 2> ${result}
	if [ "$(cat "${result}" | wc -l)" != "1" ]; then
		echo "[Fail] BPF result should have a result with cgroup filter:" "$(cat "${cgroup}")"
		err=1
		exit
	fi
}


test_csv_output()
{
	echo "Testing perf lock contention CSV output"
	perf lock contention -i ${perfdata} -E 1 -x , --output ${result}
	# count the number of commas in the header
	# it should have 5: contended, total-wait, max-wait, avg-wait, type, caller
	header=$(grep "# output:" ${result} | tr -d -c , | wc -c)
	if [ "${header}" != "5" ]; then
		echo "[Fail] Recorded result does not have enough output columns: ${header} != 5"
		err=1
		exit
	fi
	# count the number of commas in the output
	output=$(grep -v "^#" ${result} | tr -d -c , | wc -c)
	if [ "${header}" != "${output}" ]; then
		echo "[Fail] Recorded result does not match the number of commas: ${header} != ${output}"
		err=1
		exit
	fi

	if ! perf lock con -b true > /dev/null 2>&1 ; then
		echo "[Skip] No BPF support"
		return
	fi

	# the perf lock contention output goes to the stderr
	perf lock con -a -b -E 1 -x , --output ${result} -- perf bench sched messaging -p > /dev/null 2>&1
	output=$(grep -v "^#" ${result} | tr -d -c , | wc -c)
	if [ "${header}" != "${output}" ]; then
		echo "[Fail] BPF result does not match the number of commas: ${header} != ${output}"
		err=1
		exit
	fi
}

check

test_record
test_bpf
test_record_concurrent
test_aggr_task
test_aggr_addr
test_aggr_cgroup
test_type_filter
test_lock_filter
test_stack_filter
test_aggr_task_stack_filter
test_cgroup_filter
test_csv_output

cleanup
exit ${err}
