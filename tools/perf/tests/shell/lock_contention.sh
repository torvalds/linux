#!/bin/sh
# kernel lock contention analysis test
# SPDX-License-Identifier: GPL-2.0

set -e

err=0
perfdata=$(mktemp /tmp/__perf_test.perf.data.XXXXX)
result=$(mktemp /tmp/__perf_test.result.XXXXX)

cleanup() {
	rm -f ${perfdata}
	rm -f ${result}
	trap - exit term int
}

trap_cleanup() {
	cleanup
	exit ${err}
}
trap trap_cleanup exit term int

check() {
	if [ `id -u` != 0 ]; then
		echo "[Skip] No root permission"
		err=2
		exit
	fi

	if ! perf list | grep -q lock:contention_begin; then
		echo "[Skip] No lock contention tracepoints"
		err=2
		exit
	fi
}

test_record()
{
	echo "Testing perf lock record and perf lock contention"
	perf lock record -o ${perfdata} -- perf bench sched messaging > /dev/null 2>&1
	# the output goes to the stderr and we expect only 1 output (-E 1)
	perf lock contention -i ${perfdata} -E 1 -q 2> ${result}
	if [ $(cat "${result}" | wc -l) != "1" ]; then
		echo "[Fail] Recorded result count is not 1:" $(cat "${result}" | wc -l)
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
	perf lock con -a -b -E 1 -q -- perf bench sched messaging > /dev/null 2> ${result}
	if [ $(cat "${result}" | wc -l) != "1" ]; then
		echo "[Fail] BPF result count is not 1:" $(cat "${result}" | wc -l)
		err=1
		exit
	fi
}

test_record_concurrent()
{
	echo "Testing perf lock record and perf lock contention at the same time"
	perf lock record -o- -- perf bench sched messaging 2> /dev/null | \
	perf lock contention -i- -E 1 -q 2> ${result}
	if [ $(cat "${result}" | wc -l) != "1" ]; then
		echo "[Fail] Recorded result count is not 1:" $(cat "${result}" | wc -l)
		err=1
		exit
	fi
}

test_aggr_task()
{
	echo "Testing perf lock contention --threads"
	perf lock contention -i ${perfdata} -t -E 1 -q 2> ${result}
	if [ $(cat "${result}" | wc -l) != "1" ]; then
		echo "[Fail] Recorded result count is not 1:" $(cat "${result}" | wc -l)
		err=1
		exit
	fi

	if ! perf lock con -b true > /dev/null 2>&1 ; then
		return
	fi

	# the perf lock contention output goes to the stderr
	perf lock con -a -b -t -E 1 -q -- perf bench sched messaging > /dev/null 2> ${result}
	if [ $(cat "${result}" | wc -l) != "1" ]; then
		echo "[Fail] BPF result count is not 1:" $(cat "${result}" | wc -l)
		err=1
		exit
	fi
}

test_aggr_addr()
{
	echo "Testing perf lock contention --lock-addr"
	perf lock contention -i ${perfdata} -l -E 1 -q 2> ${result}
	if [ $(cat "${result}" | wc -l) != "1" ]; then
		echo "[Fail] Recorded result count is not 1:" $(cat "${result}" | wc -l)
		err=1
		exit
	fi

	if ! perf lock con -b true > /dev/null 2>&1 ; then
		return
	fi

	# the perf lock contention output goes to the stderr
	perf lock con -a -b -l -E 1 -q -- perf bench sched messaging > /dev/null 2> ${result}
	if [ $(cat "${result}" | wc -l) != "1" ]; then
		echo "[Fail] BPF result count is not 1:" $(cat "${result}" | wc -l)
		err=1
		exit
	fi
}

test_type_filter()
{
	echo "Testing perf lock contention --type-filter (w/ spinlock)"
	perf lock contention -i ${perfdata} -Y spinlock -q 2> ${result}
	if [ $(grep -c -v spinlock "${result}") != "0" ]; then
		echo "[Fail] Recorded should not have non-spinlocks:" $(cat "${result}")
		err=1
		exit
	fi

	if ! perf lock con -b true > /dev/null 2>&1 ; then
		return
	fi

	perf lock con -a -b -Y spinlock -q -- perf bench sched messaging > /dev/null 2> ${result}
	if [ $(grep -c -v spinlock "${result}") != "0" ]; then
		echo "[Fail] Recorded should not have non-spinlocks:" $(cat "${result}")
		err=1
		exit
	fi
}

test_lock_filter()
{
	echo "Testing perf lock contention --lock-filter (w/ tasklist_lock)"
	perf lock contention -i ${perfdata} -l -q 2> ${result}
	if [ $(grep -c tasklist_lock "${result}") != "1" ]; then
		echo "[Skip] Could not find 'tasklist_lock'"
		return
	fi

	perf lock contention -i ${perfdata} -L tasklist_lock -q 2> ${result}

	# find out the type of tasklist_lock
	local type=$(head -1 "${result}" | awk '{ print $8 }' | sed -e 's/:.*//')

	if [ $(grep -c -v "${type}" "${result}") != "0" ]; then
		echo "[Fail] Recorded should not have non-${type} locks:" $(cat "${result}")
		err=1
		exit
	fi

	if ! perf lock con -b true > /dev/null 2>&1 ; then
		return
	fi

	perf lock con -a -b -L tasklist_lock -q -- perf bench sched messaging > /dev/null 2> ${result}
	if [ $(grep -c -v "${type}" "${result}") != "0" ]; then
		echo "[Fail] Recorded should not have non-${type} locks:" $(cat "${result}")
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
test_type_filter
test_lock_filter

exit ${err}
