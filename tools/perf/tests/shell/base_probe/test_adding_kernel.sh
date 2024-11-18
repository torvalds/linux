#!/bin/bash
# Add 'perf probe's, list and remove them
# SPDX-License-Identifier: GPL-2.0

#
#	test_adding_kernel of perf_probe test
#	Author: Masami Hiramatsu <masami.hiramatsu.pt@hitachi.com>
#	Author: Michael Petlan <mpetlan@redhat.com>
#
#	Description:
#
#		This test tests adding of probes, their correct listing
#		and removing.
#

# include working environment
. ../common/init.sh

TEST_RESULT=0

# shellcheck source=lib/probe_vfs_getname.sh
. "$(dirname "$0")/../lib/probe_vfs_getname.sh"

TEST_PROBE=${TEST_PROBE:-"inode_permission"}

# set NO_DEBUGINFO to skip testcase if debuginfo is not present
# skip_if_no_debuginfo returns 2 if debuginfo is not present
skip_if_no_debuginfo
if [ $? -eq 2 ]; then
	NO_DEBUGINFO=1
fi

check_kprobes_available
if [ $? -ne 0 ]; then
	print_overall_skipped
	exit 0
fi


### basic probe adding

for opt in "" "-a" "--add"; do
	clear_all_probes
	$CMD_PERF probe $opt $TEST_PROBE 2> $LOGS_DIR/adding_kernel_add$opt.err
	PERF_EXIT_CODE=$?

	../common/check_all_patterns_found.pl "Added new events?:" "probe:$TEST_PROBE" "on $TEST_PROBE" < $LOGS_DIR/adding_kernel_add$opt.err
	CHECK_EXIT_CODE=$?

	print_results $PERF_EXIT_CODE $CHECK_EXIT_CODE "adding probe $TEST_PROBE :: $opt"
	(( TEST_RESULT += $? ))
done


### listing added probe :: perf list

# any added probes should appear in perf-list output
$CMD_PERF list probe:\* > $LOGS_DIR/adding_kernel_list.log
PERF_EXIT_CODE=$?

../common/check_all_lines_matched.pl "$RE_LINE_EMPTY" "List of pre-defined events" "probe:${TEST_PROBE}(?:_\d+)?\s+\[Tracepoint event\]" "Metric Groups:" < $LOGS_DIR/adding_kernel_list.log
CHECK_EXIT_CODE=$?

print_results $PERF_EXIT_CODE $CHECK_EXIT_CODE "listing added probe :: perf list"
(( TEST_RESULT += $? ))


### listing added probe :: perf probe -l

# '-l' should list all the added probes as well
$CMD_PERF probe -l > $LOGS_DIR/adding_kernel_list-l.log
PERF_EXIT_CODE=$?

../common/check_all_patterns_found.pl "\s*probe:${TEST_PROBE}(?:_\d+)?\s+\(on ${TEST_PROBE}(?:[:\+]$RE_NUMBER_HEX)?@.+\)" < $LOGS_DIR/adding_kernel_list-l.log
CHECK_EXIT_CODE=$?

if [ $NO_DEBUGINFO ] ; then
	print_testcase_skipped $NO_DEBUGINFO $NO_DEBUGINFO "Skipped due to missing debuginfo"
else
	print_results $PERF_EXIT_CODE $CHECK_EXIT_CODE "listing added probe :: perf probe -l"
fi

(( TEST_RESULT += $? ))


### using added probe

$CMD_PERF stat -e probe:$TEST_PROBE\* -o $LOGS_DIR/adding_kernel_using_probe.log -- cat /proc/uptime > /dev/null
PERF_EXIT_CODE=$?

REGEX_STAT_HEADER="\s*Performance counter stats for \'cat /proc/uptime\':"
REGEX_STAT_VALUES="\s*\d+\s+probe:$TEST_PROBE"
# the value should be greater than 1
REGEX_STAT_VALUE_NONZERO="\s*[1-9][0-9]*\s+probe:$TEST_PROBE"
REGEX_STAT_TIME="\s*$RE_NUMBER\s+seconds (?:time elapsed|user|sys)"
../common/check_all_lines_matched.pl "$REGEX_STAT_HEADER" "$REGEX_STAT_VALUES" "$REGEX_STAT_TIME" "$RE_LINE_COMMENT" "$RE_LINE_EMPTY" < $LOGS_DIR/adding_kernel_using_probe.log
CHECK_EXIT_CODE=$?
../common/check_all_patterns_found.pl "$REGEX_STAT_HEADER" "$REGEX_STAT_VALUE_NONZERO" "$REGEX_STAT_TIME" < $LOGS_DIR/adding_kernel_using_probe.log
(( CHECK_EXIT_CODE += $? ))

print_results $PERF_EXIT_CODE $CHECK_EXIT_CODE "using added probe"
(( TEST_RESULT += $? ))


### removing added probe

# '-d' should remove the probe
$CMD_PERF probe -d $TEST_PROBE\* 2> $LOGS_DIR/adding_kernel_removing.err
PERF_EXIT_CODE=$?

../common/check_all_lines_matched.pl "Removed event: probe:$TEST_PROBE" < $LOGS_DIR/adding_kernel_removing.err
CHECK_EXIT_CODE=$?

print_results $PERF_EXIT_CODE $CHECK_EXIT_CODE "deleting added probe"
(( TEST_RESULT += $? ))


### listing removed probe

# removed probes should NOT appear in perf-list output
$CMD_PERF list probe:\* > $LOGS_DIR/adding_kernel_list_removed.log
PERF_EXIT_CODE=$?

../common/check_all_lines_matched.pl "$RE_LINE_EMPTY" "List of pre-defined events" "Metric Groups:" < $LOGS_DIR/adding_kernel_list_removed.log
CHECK_EXIT_CODE=$?

print_results $PERF_EXIT_CODE $CHECK_EXIT_CODE "listing removed probe (should NOT be listed)"
(( TEST_RESULT += $? ))


### dry run

# the '-n' switch should run it in dry mode
$CMD_PERF probe -n --add $TEST_PROBE 2> $LOGS_DIR/adding_kernel_dryrun.err
PERF_EXIT_CODE=$?

# check for the output (should be the same as usual)
../common/check_all_patterns_found.pl "Added new events?:" "probe:$TEST_PROBE" "on $TEST_PROBE" < $LOGS_DIR/adding_kernel_dryrun.err
CHECK_EXIT_CODE=$?

# check that no probe was added in real
! ( $CMD_PERF probe -l | grep "probe:$TEST_PROBE" )
(( CHECK_EXIT_CODE += $? ))

print_results $PERF_EXIT_CODE $CHECK_EXIT_CODE "dry run :: adding probe"
(( TEST_RESULT += $? ))


### force-adding probes

# when using '--force' a probe should be added even if it is already there
$CMD_PERF probe --add $TEST_PROBE 2> $LOGS_DIR/adding_kernel_forceadd_01.err
PERF_EXIT_CODE=$?

../common/check_all_patterns_found.pl "Added new events?:" "probe:$TEST_PROBE" "on $TEST_PROBE" < $LOGS_DIR/adding_kernel_forceadd_01.err
CHECK_EXIT_CODE=$?

print_results $PERF_EXIT_CODE $CHECK_EXIT_CODE "force-adding probes :: first probe adding"
(( TEST_RESULT += $? ))

# adding existing probe without '--force' should fail
! $CMD_PERF probe --add $TEST_PROBE 2> $LOGS_DIR/adding_kernel_forceadd_02.err
PERF_EXIT_CODE=$?

../common/check_all_patterns_found.pl "Error: event \"$TEST_PROBE\" already exists." "Error: Failed to add events." < $LOGS_DIR/adding_kernel_forceadd_02.err
CHECK_EXIT_CODE=$?

print_results $PERF_EXIT_CODE $CHECK_EXIT_CODE "force-adding probes :: second probe adding (without force)"
(( TEST_RESULT += $? ))

# adding existing probe with '--force' should pass
NO_OF_PROBES=`$CMD_PERF probe -l | wc -l`
$CMD_PERF probe --force --add $TEST_PROBE 2> $LOGS_DIR/adding_kernel_forceadd_03.err
PERF_EXIT_CODE=$?

../common/check_all_patterns_found.pl "Added new events?:" "probe:${TEST_PROBE}_${NO_OF_PROBES}" "on $TEST_PROBE" < $LOGS_DIR/adding_kernel_forceadd_03.err
CHECK_EXIT_CODE=$?

print_results $PERF_EXIT_CODE $CHECK_EXIT_CODE "force-adding probes :: second probe adding (with force)"
(( TEST_RESULT += $? ))


### using doubled probe

# since they are the same, they should produce the same results
$CMD_PERF stat -e probe:$TEST_PROBE -e probe:${TEST_PROBE}_${NO_OF_PROBES} -x';' -o $LOGS_DIR/adding_kernel_using_two.log -- bash -c 'cat /proc/cpuinfo > /dev/null'
PERF_EXIT_CODE=$?

REGEX_LINE="$RE_NUMBER;+probe:${TEST_PROBE}_?(?:$NO_OF_PROBES)?;$RE_NUMBER;$RE_NUMBER"
../common/check_all_lines_matched.pl "$REGEX_LINE" "$RE_LINE_EMPTY" "$RE_LINE_COMMENT" < $LOGS_DIR/adding_kernel_using_two.log
CHECK_EXIT_CODE=$?

VALUE_1=`grep "$TEST_PROBE;" $LOGS_DIR/adding_kernel_using_two.log | awk -F';' '{print $1}'`
VALUE_2=`grep "${TEST_PROBE}_${NO_OF_PROBES};" $LOGS_DIR/adding_kernel_using_two.log | awk -F';' '{print $1}'`

test $VALUE_1 -eq $VALUE_2
(( CHECK_EXIT_CODE += $? ))

print_results $PERF_EXIT_CODE $CHECK_EXIT_CODE "using doubled probe"


### removing multiple probes

# using wildcards should remove all matching probes
$CMD_PERF probe --del \* 2> $LOGS_DIR/adding_kernel_removing_wildcard.err
PERF_EXIT_CODE=$?

../common/check_all_lines_matched.pl "Removed event: probe:$TEST_PROBE" "Removed event: probe:${TEST_PROBE}_1" < $LOGS_DIR/adding_kernel_removing_wildcard.err
CHECK_EXIT_CODE=$?

print_results $PERF_EXIT_CODE $CHECK_EXIT_CODE "removing multiple probes"
(( TEST_RESULT += $? ))


### wildcard adding support

$CMD_PERF probe -nf --max-probes=512 -a 'vfs_* $params' 2> $LOGS_DIR/adding_kernel_adding_wildcard.err
PERF_EXIT_CODE=$?

../common/check_all_patterns_found.pl "probe:vfs_mknod" "probe:vfs_create" "probe:vfs_rmdir" "probe:vfs_link" "probe:vfs_write" < $LOGS_DIR/adding_kernel_adding_wildcard.err
CHECK_EXIT_CODE=$?

if [ $NO_DEBUGINFO ] ; then
	print_testcase_skipped $NO_DEBUGINFO $NO_DEBUGINFO "Skipped due to missing debuginfo"
else
	print_results $PERF_EXIT_CODE $CHECK_EXIT_CODE "wildcard adding support"
fi

(( TEST_RESULT += $? ))


### non-existing variable

# perf probe should survive a non-existing variable probing attempt
{ $CMD_PERF probe 'vfs_read somenonexistingrandomstuffwhichisalsoprettylongorevenlongertoexceed64' ; } 2> $LOGS_DIR/adding_kernel_nonexisting.err
PERF_EXIT_CODE=$?

# the exitcode should not be 0 or segfault
test $PERF_EXIT_CODE -ne 139 -a $PERF_EXIT_CODE -ne 0
PERF_EXIT_CODE=$?

# check that the error message is reasonable
../common/check_all_patterns_found.pl "Failed to find" "somenonexistingrandomstuffwhichisalsoprettylongorevenlongertoexceed64" < $LOGS_DIR/adding_kernel_nonexisting.err
CHECK_EXIT_CODE=$?
../common/check_all_patterns_found.pl "in this function|at this address" "Error" "Failed to add events" < $LOGS_DIR/adding_kernel_nonexisting.err
(( CHECK_EXIT_CODE += $? ))
../common/check_all_lines_matched.pl "Failed to find" "Error" "Probe point .+ not found" "optimized out" "Use.+\-\-range option to show.+location range" < $LOGS_DIR/adding_kernel_nonexisting.err
(( CHECK_EXIT_CODE += $? ))
../common/check_no_patterns_found.pl "$RE_SEGFAULT" < $LOGS_DIR/adding_kernel_nonexisting.err
(( CHECK_EXIT_CODE += $? ))

if [ $NO_DEBUGINFO ]; then
	print_testcase_skipped $NO_DEBUGINFO $NO_DEBUGINFO "Skipped due to missing debuginfo"
else
	print_results $PERF_EXIT_CODE $CHECK_EXIT_CODE "non-existing variable"
fi

(( TEST_RESULT += $? ))


### function with return value

# adding probe with return value
$CMD_PERF probe --add "$TEST_PROBE%return \$retval" 2> $LOGS_DIR/adding_kernel_func_retval_add.err
PERF_EXIT_CODE=$?

../common/check_all_patterns_found.pl "Added new events?:" "probe:$TEST_PROBE" "on $TEST_PROBE%return with \\\$retval" < $LOGS_DIR/adding_kernel_func_retval_add.err
CHECK_EXIT_CODE=$?

print_results $PERF_EXIT_CODE $CHECK_EXIT_CODE "function with retval :: add"
(( TEST_RESULT += $? ))

# recording some data
$CMD_PERF record -e probe:$TEST_PROBE\* -o $CURRENT_TEST_DIR/perf.data -- cat /proc/cpuinfo > /dev/null 2> $LOGS_DIR/adding_kernel_func_retval_record.err
PERF_EXIT_CODE=$?

../common/check_all_patterns_found.pl "$RE_LINE_RECORD1" "$RE_LINE_RECORD2" < $LOGS_DIR/adding_kernel_func_retval_record.err
CHECK_EXIT_CODE=$?

print_results $PERF_EXIT_CODE $CHECK_EXIT_CODE "function with retval :: record"
(( TEST_RESULT += $? ))

# perf script should report the function calls with the correct arg values
$CMD_PERF script -i $CURRENT_TEST_DIR/perf.data > $LOGS_DIR/adding_kernel_func_retval_script.log
PERF_EXIT_CODE=$?

REGEX_SCRIPT_LINE="\s*cat\s+$RE_NUMBER\s+\[$RE_NUMBER\]\s+$RE_NUMBER:\s+probe:$TEST_PROBE\w*:\s+\($RE_NUMBER_HEX\s+<\-\s+$RE_NUMBER_HEX\)\s+arg1=$RE_NUMBER_HEX"
../common/check_all_lines_matched.pl "$REGEX_SCRIPT_LINE" < $LOGS_DIR/adding_kernel_func_retval_script.log
CHECK_EXIT_CODE=$?
../common/check_all_patterns_found.pl "$REGEX_SCRIPT_LINE" < $LOGS_DIR/adding_kernel_func_retval_script.log
(( CHECK_EXIT_CODE += $? ))

print_results $PERF_EXIT_CODE $CHECK_EXIT_CODE "function argument probing :: script"
(( TEST_RESULT += $? ))


clear_all_probes

# print overall results
print_overall_results "$TEST_RESULT"
exit $?
