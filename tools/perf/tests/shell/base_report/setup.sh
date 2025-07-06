#!/bin/bash
# perftool-testsuite :: perf_report
# SPDX-License-Identifier: GPL-2.0

#
#	setup.sh of perf report test
#	Author: Michael Petlan <mpetlan@redhat.com>
#
#	Description:
#
#		We need some sample data for perf-report testing
#
#

# include working environment
. ../common/init.sh

TEST_RESULT=0

test -d "$HEADER_TAR_DIR" || mkdir -p "$HEADER_TAR_DIR"

SW_EVENT="cpu-clock"

$CMD_PERF record -asdg -e $SW_EVENT -o $CURRENT_TEST_DIR/perf.data -- $CMD_LONGER_SLEEP 2> $LOGS_DIR/setup.log
PERF_EXIT_CODE=$?

../common/check_all_patterns_found.pl "$RE_LINE_RECORD1" "$RE_LINE_RECORD2" < $LOGS_DIR/setup.log
CHECK_EXIT_CODE=$?

print_results $PERF_EXIT_CODE $CHECK_EXIT_CODE "prepare the perf.data file"
(( TEST_RESULT += $? ))

# Some minimal parallel workload.
$CMD_PERF record --latency -o $CURRENT_TEST_DIR/perf.data.1 bash -c "for i in {1..100} ; do cat /proc/cpuinfo 1> /dev/null & done; sleep 1" 2> $LOGS_DIR/setup-latency.log
PERF_EXIT_CODE=$?

echo ==================
cat $LOGS_DIR/setup-latency.log
echo ==================

../common/check_all_patterns_found.pl "$RE_LINE_RECORD1" "$RE_LINE_RECORD2" < $LOGS_DIR/setup-latency.log
CHECK_EXIT_CODE=$?

print_results $PERF_EXIT_CODE $CHECK_EXIT_CODE "prepare the perf.data.1 file"
(( TEST_RESULT += $? ))

print_overall_results $TEST_RESULT
exit $?
