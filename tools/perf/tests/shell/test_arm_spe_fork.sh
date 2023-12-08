#!/bin/sh
# Check Arm SPE doesn't hang when there are forks

# SPDX-License-Identifier: GPL-2.0
# German Gomez <german.gomez@arm.com>, 2022

skip_if_no_arm_spe_event() {
	perf list | egrep -q 'arm_spe_[0-9]+//' && return 0
	return 2
}

skip_if_no_arm_spe_event || exit 2

# skip if there's no compiler
if ! [ -x "$(command -v cc)" ]; then
	echo "failed: no compiler, install gcc"
	exit 2
fi

TEST_PROGRAM_SOURCE=$(mktemp /tmp/__perf_test.program.XXXXX.c)
TEST_PROGRAM=$(mktemp /tmp/__perf_test.program.XXXXX)
PERF_DATA=$(mktemp /tmp/__perf_test.perf.data.XXXXX)
PERF_RECORD_LOG=$(mktemp /tmp/__perf_test.log.XXXXX)

cleanup_files()
{
	echo "Cleaning up files..."
	rm -f ${PERF_RECORD_LOG}
	rm -f ${PERF_DATA}
	rm -f ${TEST_PROGRAM_SOURCE}
	rm -f ${TEST_PROGRAM}
}

trap cleanup_files exit term int

# compile test program
cat << EOF > $TEST_PROGRAM_SOURCE
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/wait.h>

int workload() {
  while (1)
    sqrt(rand());
  return 0;
}

int main() {
  switch (fork()) {
    case 0:
      return workload();
    case -1:
      return 1;
    default:
      wait(NULL);
  }
  return 0;
}
EOF

echo "Compiling test program..."
CFLAGS="-lm"
cc $TEST_PROGRAM_SOURCE $CFLAGS -o $TEST_PROGRAM || exit 1

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
# test program may leave an orphan process running the workload
killall $(basename $TEST_PROGRAM)

if [ "$log0" = "$log1" ];
then
        echo "SPE hang test: FAIL"
        exit 1
else
        echo "SPE hang test: PASS"
fi

exit 0
