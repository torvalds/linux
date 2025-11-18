#!/bin/bash
# perf_probe :: Reject invalid options (exclusive)
# SPDX-License-Identifier: GPL-2.0

#
#	test_invalid_options of perf_probe test
#	Author: Masami Hiramatsu <masami.hiramatsu.pt@hitachi.com>
#	Author: Michael Petlan <mpetlan@redhat.com>
#
#	Description:
#
#		This test checks whether the invalid and incompatible options are reported
#

DIR_PATH="$(dirname $0)"
TEST_RESULT=0

# include working environment
. "$DIR_PATH/../common/init.sh"

if ! check_kprobes_available; then
	print_overall_skipped
	exit 2
fi

# Check for presence of DWARF
$CMD_PERF check feature -q dwarf
[ $? -ne 0 ] && HINT_FAIL="Some of the tests need DWARF to run"

### missing argument

# some options require an argument
for opt in '-a' '-d' '-L' '-V'; do
	! $CMD_PERF probe $opt 2> $LOGS_DIR/invalid_options_missing_argument$opt.err
	PERF_EXIT_CODE=$?

	"$DIR_PATH/../common/check_all_patterns_found.pl" \
		"Error: switch .* requires a value" \
		< $LOGS_DIR/invalid_options_missing_argument$opt.err
	CHECK_EXIT_CODE=$?

	print_results $PERF_EXIT_CODE $CHECK_EXIT_CODE "missing argument for $opt"
	(( TEST_RESULT += $? ))
done


### unnecessary argument

# some options may omit the argument
for opt in '-F' '-l'; do
	$CMD_PERF probe -F > /dev/null 2> $LOGS_DIR/invalid_options_unnecessary_argument$opt.err
	PERF_EXIT_CODE=$?

	test ! -s $LOGS_DIR/invalid_options_unnecessary_argument$opt.err
	CHECK_EXIT_CODE=$?

	print_results $PERF_EXIT_CODE $CHECK_EXIT_CODE "unnecessary argument for $opt"
	(( TEST_RESULT += $? ))
done


### mutually exclusive options

# some options are mutually exclusive
test -e $LOGS_DIR/invalid_options_mutually_exclusive.log && rm -f $LOGS_DIR/invalid_options_mutually_exclusive.log
for opt in '-a xxx -d xxx' '-a xxx -L foo' '-a xxx -V foo' '-a xxx -l' '-a xxx -F' \
		'-d xxx -L foo' '-d xxx -V foo' '-d xxx -l' '-d xxx -F' \
		'-L foo -V bar' '-L foo -l' '-L foo -F' '-V foo -l' '-V foo -F' '-l -F'; do
	! $CMD_PERF probe $opt > /dev/null 2> $LOGS_DIR/aux.log
	PERF_EXIT_CODE=$?

	"$DIR_PATH/../common/check_all_patterns_found.pl" \
		"Error: switch .+ cannot be used with switch .+" < $LOGS_DIR/aux.log
	CHECK_EXIT_CODE=$?

	print_results $PERF_EXIT_CODE $CHECK_EXIT_CODE "mutually exclusive options :: $opt"
	(( TEST_RESULT += $? ))

	# gather the logs
	cat $LOGS_DIR/aux.log | grep "Error" >> $LOGS_DIR/invalid_options_mutually_exclusive.log
done


# print overall results
print_overall_results "$TEST_RESULT" $HINT_FAIL
exit $?
