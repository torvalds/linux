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
BLACKFUNC_LIST=`head -n 5 /sys/kernel/debug/kprobes/blacklist 2> /dev/null | cut -f2`
if [ -z "$BLACKFUNC_LIST" ]; then
	print_overall_skipped
	exit 0
fi

# try to find vmlinux with DWARF debug info
VMLINUX_FILE=$(perf probe -v random_probe |& grep "Using.*for symbols" | sed -r 's/^Using (.*) for symbols$/\1/')

# remove all previously added probes
clear_all_probes


### adding blacklisted function
REGEX_SCOPE_FAIL="Failed to find scope of probe point"
REGEX_SKIP_MESSAGE=" is blacklisted function, skip it\."
REGEX_NOT_FOUND_MESSAGE="Probe point \'$RE_EVENT\' not found."
REGEX_ERROR_MESSAGE="Error: Failed to add events."
REGEX_INVALID_ARGUMENT="Failed to write event: Invalid argument"
REGEX_SYMBOL_FAIL="Failed to find symbol at $RE_ADDRESS"
REGEX_OUT_SECTION="$RE_EVENT is out of \.\w+, skip it"
REGEX_MISSING_DECL_LINE="A function DIE doesn't have decl_line. Maybe broken DWARF?"

BLACKFUNC=""
SKIP_DWARF=0

for BLACKFUNC in $BLACKFUNC_LIST; do
	echo "Probing $BLACKFUNC"

	# functions from blacklist should be skipped by perf probe
	! $CMD_PERF probe $BLACKFUNC > $LOGS_DIR/adding_blacklisted.log 2> $LOGS_DIR/adding_blacklisted.err
	PERF_EXIT_CODE=$?

	# check for bad DWARF polluting the result
	../common/check_all_patterns_found.pl "$REGEX_MISSING_DECL_LINE" >/dev/null < $LOGS_DIR/adding_blacklisted.err

	if [ $? -eq 0 ]; then
		SKIP_DWARF=1
		echo "Result polluted by broken DWARF, trying another probe"

		# confirm that the broken DWARF comes from assembler
		if [ -n "$VMLINUX_FILE" ]; then
			readelf -wi "$VMLINUX_FILE" |
			awk -v probe="$BLACKFUNC" '/DW_AT_language/ { comp_lang = $0 }
						   $0 ~ probe { if (comp_lang) { print comp_lang }; exit }' |
			grep -q "MIPS assembler"

			CHECK_EXIT_CODE=$?
			if [ $CHECK_EXIT_CODE -ne 0 ]; then
				SKIP_DWARF=0 # broken DWARF while available
				break
			fi
		fi
	else
		../common/check_all_lines_matched.pl "$REGEX_SKIP_MESSAGE" "$REGEX_NOT_FOUND_MESSAGE" "$REGEX_ERROR_MESSAGE" "$REGEX_SCOPE_FAIL" "$REGEX_INVALID_ARGUMENT" "$REGEX_SYMBOL_FAIL" "$REGEX_OUT_SECTION" < $LOGS_DIR/adding_blacklisted.err
		CHECK_EXIT_CODE=$?

		SKIP_DWARF=0
		break
	fi
done

if [ $SKIP_DWARF -eq 1 ]; then
	print_testcase_skipped "adding blacklisted function $BLACKFUNC"
else
	print_results $PERF_EXIT_CODE $CHECK_EXIT_CODE "adding blacklisted function $BLACKFUNC"
	(( TEST_RESULT += $? ))
fi

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
