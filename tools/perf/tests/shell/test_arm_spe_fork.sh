#!/bin/sh
# Check Arm SPE doesn't hang when there are forks

# SPDX-License-Identifier: GPL-2.0
# German Gomez <german.gomez@arm.com>, 2022

skip_if_no_arm_spe_event() {
	perf list | grep -E -q 'arm_spe_[0-9]+//' && return 0
	return 2
}

skip_if_no_arm_spe_event || exit 2

TEST_PROGRAM="perf test -w sqrtloop 10"
PERF_DATA=$(mktemp /tmp/__perf_test.perf.data.XXXXX)
PERF_RECORD_LOG=$(mktemp /tmp/__perf_test.log.XXXXX)

cleanup_files()
{
	echo "Cleaning up files..."
	rm -f ${PERF_RECORD_LOG}
	rm -f ${PERF_DATA}
}

trap cleanup_files exit term int

echo "Recording workload..."
perf record -o ${PERF_DATA} -e arm_spe/period=65536/ -vvv -- $TEST_PROGRAM > ${PERF_RECORD_LOG} 2>&1 &
PERFPID=$!

# Check if perf hangs by checking the perf-record logs.
sleep 1
log0=$(wc -l $PERF_RECORD_LOG)
echo Log lines = $log0
sleep 1
log1=$(wc -l $PERF_RECORD_LOG)
echo Log lines after 1 second = $log1

kill $PERFPID
wait $PERFPID

if [ "$log0" = "$log1" ];
then
        echo "SPE hang test: FAIL"
        exit 1
else
        echo "SPE hang test: PASS"
fi

exit 0
