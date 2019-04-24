#!/bin/sh
# SPDX-License-Identifier: GPL-2.0
#
# Runs a set of tests in a given subdirectory.
export KSFT_TAP_LEVEL=1
export skip_rc=4
export logfile=/dev/stdout
export per_test_logging=

run_one()
{
	DIR="$1"
	TEST="$2"
	NUM="$3"

	BASENAME_TEST=$(basename $TEST)

	TEST_HDR_MSG="selftests: $DIR: $BASENAME_TEST"
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

run_many()
{
	echo "TAP version 13"
	DIR=$(basename "$PWD")
	test_num=0
	for TEST in "$@"; do
		BASENAME_TEST=$(basename $TEST)
		test_num=$(( test_num + 1 ))
		if [ -n "$per_test_logging" ]; then
			logfile="/tmp/$BASENAME_TEST"
			cat /dev/null > "$logfile"
		fi
		run_one "$DIR" "$TEST" "$test_num"
	done
}
