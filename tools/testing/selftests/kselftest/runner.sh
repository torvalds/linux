#!/bin/sh
# SPDX-License-Identifier: GPL-2.0
#
# Runs a set of tests in a given subdirectory.
export skip_rc=4
export logfile=/dev/stdout
export per_test_logging=

# There isn't a shell-agnostic way to find the path of a sourced file,
# so we must rely on BASE_DIR being set to find other tools.
if [ -z "$BASE_DIR" ]; then
	echo "Error: BASE_DIR must be set before sourcing." >&2
	exit 1
fi

# If Perl is unavailable, we must fall back to line-at-a-time prefixing
# with sed instead of unbuffered output.
tap_prefix()
{
	if [ ! -x /usr/bin/perl ]; then
		sed -e 's/^/# /'
	else
		"$BASE_DIR"/kselftest/prefix.pl
	fi
}

run_one()
{
	DIR="$1"
	TEST="$2"
	NUM="$3"

	BASENAME_TEST=$(basename $TEST)

	TEST_HDR_MSG="selftests: $DIR: $BASENAME_TEST"
	echo "# $TEST_HDR_MSG"
	if [ ! -x "$TEST" ]; then
		echo -n "# Warning: file $TEST is "
		if [ ! -e "$TEST" ]; then
			echo "missing!"
		else
			echo "not executable, correct this."
		fi
		echo "not ok $test_num $TEST_HDR_MSG"
	else
		cd `dirname $TEST` > /dev/null
		(((((./$BASENAME_TEST 2>&1; echo $? >&3) |
			tap_prefix >&4) 3>&1) |
			(read xs; exit $xs)) 4>>"$logfile" &&
		echo "ok $test_num $TEST_HDR_MSG") ||
		(if [ $? -eq $skip_rc ]; then	\
			echo "not ok $test_num $TEST_HDR_MSG # SKIP"
		else
			echo "not ok $test_num $TEST_HDR_MSG"
		fi)
		cd - >/dev/null
	fi
}

run_many()
{
	echo "TAP version 13"
	DIR=$(basename "$PWD")
	test_num=0
	total=$(echo "$@" | wc -w)
	echo "1..$total"
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
