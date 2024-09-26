#!/bin/bash

# SPDX-License-Identifier: GPL-2.0

#
#	test_adding_blacklisted of perf_probe test
#	Author: Masami Hiramatsu <masami.hiramatsu.pt@hitachi.com>
#	Author: Michael Petlan <mpetlan@redhat.com>
#
#	Description:
#
#		Blacklisted functions should not be added successfully as probes,
#	they must be skipped.
#

# include working environment
. ../common/init.sh

TEST_RESULT=0

# skip if not supported
BLACKFUNC=`head -n 1 /sys/kernel/debug/kprobes/blacklist 2> /dev/null | cut -f2`
if [ -z "$BLACKFUNC" ]; then
	print_overall_skipped
	exit 0
fi

# remove all previously added probes
clear_all_probes


### adding blacklisted function

# functions from blacklist should be skipped by perf probe
! $CMD_PERF probe $BLACKFUNC > $LOGS_DIR/adding_blacklisted.log 2> $LOGS_DIR/adding_blacklisted.err
PERF_EXIT_CODE=$?

REGEX_SCOPE_FAIL="Failed to find scope of probe point"
REGEX_SKIP_MESSAGE=" is blacklisted function, skip it\."
REGEX_NOT_FOUND_MESSAGE="Probe point \'$BLACKFUNC\' not found."
REGEX_ERROR_MESSAGE="Error: Failed to add events."
REGEX_INVALID_ARGUMENT="Failed to write event: Invalid argument"
REGEX_SYMBOL_FAIL="Failed to find symbol at $RE_ADDRESS"
REGEX_OUT_SECTION="$BLACKFUNC is out of \.\w+, skip it"
../common/check_all_lines_matched.pl "$REGEX_SKIP_MESSAGE" "$REGEX_NOT_FOUND_MESSAGE" "$REGEX_ERROR_MESSAGE" "$REGEX_SCOPE_FAIL" "$REGEX_INVALID_ARGUMENT" "$REGEX_SYMBOL_FAIL" "$REGEX_OUT_SECTION" < $LOGS_DIR/adding_blacklisted.err
CHECK_EXIT_CODE=$?

print_results $PERF_EXIT_CODE $CHECK_EXIT_CODE "adding blacklisted function $BLACKFUNC"
(( TEST_RESULT += $? ))


### listing not-added probe

# blacklisted probes should NOT appear in perf-list output
$CMD_PERF list probe:\* > $LOGS_DIR/adding_blacklisted_list.log
PERF_EXIT_CODE=$?

../common/check_all_lines_matched.pl "$RE_LINE_EMPTY" "List of pre-defined events" "Metric Groups:" < $LOGS_DIR/adding_blacklisted_list.log
CHECK_EXIT_CODE=$?

print_results $PERF_EXIT_CODE $CHECK_EXIT_CODE "listing blacklisted probe (should NOT be listed)"
(( TEST_RESULT += $? ))


# print overall results
print_overall_results "$TEST_RESULT"
exit $?
