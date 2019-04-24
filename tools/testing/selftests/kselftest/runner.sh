#!/bin/sh
# SPDX-License-Identifier: GPL-2.0
#
# Runs a set of tests in a given subdirectory.
export skip_rc=4
export logfile=/dev/stdout

run_one()
{
	TEST="$1"
	NUM="$2"

	BASENAME_TEST=$(basename $TEST)

	TEST_HDR_MSG="selftests: "`basename $PWD`:" $BASENAME_TEST"
	echo "$TEST_HDR_MSG"
	echo "========================================"
	if [ ! -x "$TEST" ]; then
		echo "$TEST_HDR_MSG: Warning: file $TEST is not executable, correct this."
		echo "not ok 1..$test_num $TEST_HDR_MSG [FAIL]"
	else
		cd `dirname $TEST` > /dev/null
		(./$BASENAME_TEST >> "$logfile" 2>&1 &&
		echo "ok 1..$test_num $TEST_HDR_MSG [PASS]") ||
		(if [ $? -eq $skip_rc ]; then	\
			echo "not ok 1..$test_num $TEST_HDR_MSG [SKIP]"
		else
			echo "not ok 1..$test_num $TEST_HDR_MSG [FAIL]"
		fi)
		cd - >/dev/null
	fi
}
