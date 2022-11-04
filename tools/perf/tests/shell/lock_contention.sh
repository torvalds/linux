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

check

test_record
test_bpf
test_record_concurrent

exit ${err}
