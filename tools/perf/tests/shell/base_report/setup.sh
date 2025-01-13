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

test -d "$HEADER_TAR_DIR" || mkdir -p "$HEADER_TAR_DIR"

SW_EVENT="cpu-clock"

$CMD_PERF record -asdg -e $SW_EVENT -o $CURRENT_TEST_DIR/perf.data -- $CMD_LONGER_SLEEP 2> $LOGS_DIR/setup.log
PERF_EXIT_CODE=$?

../common/check_all_patterns_found.pl "$RE_LINE_RECORD1" "$RE_LINE_RECORD2" < $LOGS_DIR/setup.log
CHECK_EXIT_CODE=$?

print_results $PERF_EXIT_CODE $CHECK_EXIT_CODE "prepare the perf.data file"
TEST_RESULT=$?

print_overall_results $TEST_RESULT
exit $?
