#!/bin/bash
# Copyright (C) 2017 Luis R. Rodriguez <mcgrof@kernel.org>
#
# This program is free software; you can redistribute it and/or modify it
# under the terms of the GNU General Public License as published by the Free
# Software Foundation; either version 2 of the License, or at your option any
# later version; or, when distributed separately from the Linux kernel or
# when incorporated into other software packages, subject to the following
# license:
#
# This program is free software; you can redistribute it and/or modify it
# under the terms of copyleft-next (version 0.3.1 or later) as published
# at http://copyleft-next.org/.

# This performs a series tests against the proc sysctl interface.

TEST_NAME="sysctl"
TEST_DRIVER="test_${TEST_NAME}"
TEST_DIR=$(dirname $0)
TEST_FILE=$(mktemp)

# This represents
#
# TEST_ID:TEST_COUNT:ENABLED
#
# TEST_ID: is the test id number
# TEST_COUNT: number of times we should run the test
# ENABLED: 1 if enabled, 0 otherwise
#
# Once these are enabled please leave them as-is. Write your own test,
# we have tons of space.
ALL_TESTS="0001:1:1"
ALL_TESTS="$ALL_TESTS 0002:1:1"
ALL_TESTS="$ALL_TESTS 0003:1:1"

test_modprobe()
{
       if [ ! -d $DIR ]; then
               echo "$0: $DIR not present" >&2
               echo "You must have the following enabled in your kernel:" >&2
               cat $TEST_DIR/config >&2
               exit 1
       fi
}

function allow_user_defaults()
{
	if [ -z $DIR ]; then
		DIR="/sys/module/test_sysctl/"
	fi
	if [ -z $DEFAULT_NUM_TESTS ]; then
		DEFAULT_NUM_TESTS=50
	fi
	if [ -z $SYSCTL ]; then
		SYSCTL="/proc/sys/debug/test_sysctl"
	fi
	if [ -z $PROD_SYSCTL ]; then
		PROD_SYSCTL="/proc/sys"
	fi
	if [ -z $WRITES_STRICT ]; then
		WRITES_STRICT="${PROD_SYSCTL}/kernel/sysctl_writes_strict"
	fi
}

function check_production_sysctl_writes_strict()
{
	echo -n "Checking production write strict setting ... "
	if [ ! -e ${WRITES_STRICT} ]; then
		echo "FAIL, but skip in case of old kernel" >&2
	else
		old_strict=$(cat ${WRITES_STRICT})
		if [ "$old_strict" = "1" ]; then
			echo "ok"
		else
			echo "FAIL, strict value is 0 but force to 1 to continue" >&2
			echo "1" > ${WRITES_STRICT}
		fi
	fi

	if [ -z $PAGE_SIZE ]; then
		PAGE_SIZE=$(getconf PAGESIZE)
	fi
	if [ -z $MAX_DIGITS ]; then
		MAX_DIGITS=$(($PAGE_SIZE/8))
	fi
	if [ -z $INT_MAX ]; then
		INT_MAX=$(getconf INT_MAX)
	fi
}

test_reqs()
{
	uid=$(id -u)
	if [ $uid -ne 0 ]; then
		echo $msg must be run as root >&2
		exit 0
	fi

	if ! which perl 2> /dev/null > /dev/null; then
		echo "$0: You need perl installed"
		exit 1
	fi
	if ! which getconf 2> /dev/null > /dev/null; then
		echo "$0: You need getconf installed"
		exit 1
	fi
}

function load_req_mod()
{
	trap "test_modprobe" EXIT

	if [ ! -d $DIR ]; then
		modprobe $TEST_DRIVER
		if [ $? -ne 0 ]; then
			exit
		fi
	fi
}

reset_vals()
{
	VAL=""
	TRIGGER=$(basename ${TARGET})
	case "$TRIGGER" in
		int_0001)
			VAL="60"
			;;
		int_0002)
			VAL="1"
			;;
		string_0001)
			VAL="(none)"
			;;
		*)
			;;
	esac
	echo -n $VAL > $TARGET
}

set_orig()
{
	if [ ! -z $TARGET ]; then
		echo "${ORIG}" > "${TARGET}"
	fi
}

set_test()
{
	echo "${TEST_STR}" > "${TARGET}"
}

verify()
{
	local seen
	seen=$(cat "$1")
	if [ "${seen}" != "${TEST_STR}" ]; then
		return 1
	fi
	return 0
}

test_rc()
{
	if [[ $rc != 0 ]]; then
		echo "Failed test, return value: $rc" >&2
		exit $rc
	fi
}

test_finish()
{
	set_orig
	rm -f "${TEST_FILE}"

	if [ ! -z ${old_strict} ]; then
		echo ${old_strict} > ${WRITES_STRICT}
	fi
	exit $rc
}

run_numerictests()
{
	echo "== Testing sysctl behavior against ${TARGET} =="

	rc=0

	echo -n "Writing test file ... "
	echo "${TEST_STR}" > "${TEST_FILE}"
	if ! verify "${TEST_FILE}"; then
		echo "FAIL" >&2
		exit 1
	else
		echo "ok"
	fi

	echo -n "Checking sysctl is not set to test value ... "
	if verify "${TARGET}"; then
		echo "FAIL" >&2
		exit 1
	else
		echo "ok"
	fi

	echo -n "Writing sysctl from shell ... "
	set_test
	if ! verify "${TARGET}"; then
		echo "FAIL" >&2
		exit 1
	else
		echo "ok"
	fi

	echo -n "Resetting sysctl to original value ... "
	set_orig
	if verify "${TARGET}"; then
		echo "FAIL" >&2
		exit 1
	else
		echo "ok"
	fi

	# Now that we've validated the sanity of "set_test" and "set_orig",
	# we can use those functions to set starting states before running
	# specific behavioral tests.

	echo -n "Writing entire sysctl in single write ... "
	set_orig
	dd if="${TEST_FILE}" of="${TARGET}" bs=4096 2>/dev/null
	if ! verify "${TARGET}"; then
		echo "FAIL" >&2
		rc=1
	else
		echo "ok"
	fi

	echo -n "Writing middle of sysctl after synchronized seek ... "
	set_test
	dd if="${TEST_FILE}" of="${TARGET}" bs=1 seek=1 skip=1 2>/dev/null
	if ! verify "${TARGET}"; then
		echo "FAIL" >&2
		rc=1
	else
		echo "ok"
	fi

	echo -n "Writing beyond end of sysctl ... "
	set_orig
	dd if="${TEST_FILE}" of="${TARGET}" bs=20 seek=2 2>/dev/null
	if verify "${TARGET}"; then
		echo "FAIL" >&2
		rc=1
	else
		echo "ok"
	fi

	echo -n "Writing sysctl with multiple long writes ... "
	set_orig
	(perl -e 'print "A" x 50;'; echo "${TEST_STR}") | \
		dd of="${TARGET}" bs=50 2>/dev/null
	if verify "${TARGET}"; then
		echo "FAIL" >&2
		rc=1
	else
		echo "ok"
	fi
	test_rc
}

# Your test must accept digits 3 and 4 to use this
run_limit_digit()
{
	echo -n "Checking ignoring spaces up to PAGE_SIZE works on write ..."
	reset_vals

	LIMIT=$((MAX_DIGITS -1))
	TEST_STR="3"
	(perl -e 'print " " x '$LIMIT';'; echo "${TEST_STR}") | \
		dd of="${TARGET}" 2>/dev/null

	if ! verify "${TARGET}"; then
		echo "FAIL" >&2
		rc=1
	else
		echo "ok"
	fi
	test_rc

	echo -n "Checking passing PAGE_SIZE of spaces fails on write ..."
	reset_vals

	LIMIT=$((MAX_DIGITS))
	TEST_STR="4"
	(perl -e 'print " " x '$LIMIT';'; echo "${TEST_STR}") | \
		dd of="${TARGET}" 2>/dev/null

	if verify "${TARGET}"; then
		echo "FAIL" >&2
		rc=1
	else
		echo "ok"
	fi
	test_rc
}

# You are using an int
run_limit_digit_int()
{
	echo -n "Testing INT_MAX works ..."
	reset_vals
	TEST_STR="$INT_MAX"
	echo -n $TEST_STR > $TARGET

	if ! verify "${TARGET}"; then
		echo "FAIL" >&2
		rc=1
	else
		echo "ok"
	fi
	test_rc

	echo -n "Testing INT_MAX + 1 will fail as expected..."
	reset_vals
	let TEST_STR=$INT_MAX+1
	echo -n $TEST_STR > $TARGET 2> /dev/null

	if verify "${TARGET}"; then
		echo "FAIL" >&2
		rc=1
	else
		echo "ok"
	fi
	test_rc

	echo -n "Testing negative values will work as expected..."
	reset_vals
	TEST_STR="-3"
	echo -n $TEST_STR > $TARGET 2> /dev/null
	if ! verify "${TARGET}"; then
		echo "FAIL" >&2
		rc=1
	else
		echo "ok"
	fi
	test_rc
}

run_stringtests()
{
	echo -n "Writing entire sysctl in short writes ... "
	set_orig
	dd if="${TEST_FILE}" of="${TARGET}" bs=1 2>/dev/null
	if ! verify "${TARGET}"; then
		echo "FAIL" >&2
		rc=1
	else
		echo "ok"
	fi

	echo -n "Writing middle of sysctl after unsynchronized seek ... "
	set_test
	dd if="${TEST_FILE}" of="${TARGET}" bs=1 seek=1 2>/dev/null
	if verify "${TARGET}"; then
		echo "FAIL" >&2
		rc=1
	else
		echo "ok"
	fi

	echo -n "Checking sysctl maxlen is at least $MAXLEN ... "
	set_orig
	perl -e 'print "A" x ('"${MAXLEN}"'-2), "B";' | \
		dd of="${TARGET}" bs="${MAXLEN}" 2>/dev/null
	if ! grep -q B "${TARGET}"; then
		echo "FAIL" >&2
		rc=1
	else
		echo "ok"
	fi

	echo -n "Checking sysctl keeps original string on overflow append ... "
	set_orig
	perl -e 'print "A" x ('"${MAXLEN}"'-1), "B";' | \
		dd of="${TARGET}" bs=$(( MAXLEN - 1 )) 2>/dev/null
	if grep -q B "${TARGET}"; then
		echo "FAIL" >&2
		rc=1
	else
		echo "ok"
	fi

	echo -n "Checking sysctl stays NULL terminated on write ... "
	set_orig
	perl -e 'print "A" x ('"${MAXLEN}"'-1), "B";' | \
		dd of="${TARGET}" bs="${MAXLEN}" 2>/dev/null
	if grep -q B "${TARGET}"; then
		echo "FAIL" >&2
		rc=1
	else
		echo "ok"
	fi

	echo -n "Checking sysctl stays NULL terminated on overwrite ... "
	set_orig
	perl -e 'print "A" x ('"${MAXLEN}"'-1), "BB";' | \
		dd of="${TARGET}" bs=$(( $MAXLEN + 1 )) 2>/dev/null
	if grep -q B "${TARGET}"; then
		echo "FAIL" >&2
		rc=1
	else
		echo "ok"
	fi

	test_rc
}

sysctl_test_0001()
{
	TARGET="${SYSCTL}/int_0001"
	reset_vals
	ORIG=$(cat "${TARGET}")
	TEST_STR=$(( $ORIG + 1 ))

	run_numerictests
	run_limit_digit
}

sysctl_test_0002()
{
	TARGET="${SYSCTL}/string_0001"
	reset_vals
	ORIG=$(cat "${TARGET}")
	TEST_STR="Testing sysctl"
	# Only string sysctls support seeking/appending.
	MAXLEN=65

	run_numerictests
	run_stringtests
}

sysctl_test_0003()
{
	TARGET="${SYSCTL}/int_0002"
	reset_vals
	ORIG=$(cat "${TARGET}")
	TEST_STR=$(( $ORIG + 1 ))

	run_numerictests
	run_limit_digit
	run_limit_digit_int
}

list_tests()
{
	echo "Test ID list:"
	echo
	echo "TEST_ID x NUM_TEST"
	echo "TEST_ID:   Test ID"
	echo "NUM_TESTS: Number of recommended times to run the test"
	echo
	echo "0001 x $(get_test_count 0001) - tests proc_dointvec_minmax()"
	echo "0002 x $(get_test_count 0002) - tests proc_dostring()"
	echo "0003 x $(get_test_count 0003) - tests proc_dointvec()"
}

test_reqs

usage()
{
	NUM_TESTS=$(grep -o ' ' <<<"$ALL_TESTS" | grep -c .)
	let NUM_TESTS=$NUM_TESTS+1
	MAX_TEST=$(printf "%04d\n" $NUM_TESTS)
	echo "Usage: $0 [ -t <4-number-digit> ] | [ -w <4-number-digit> ] |"
	echo "		 [ -s <4-number-digit> ] | [ -c <4-number-digit> <test- count>"
	echo "           [ all ] [ -h | --help ] [ -l ]"
	echo ""
	echo "Valid tests: 0001-$MAX_TEST"
	echo ""
	echo "    all     Runs all tests (default)"
	echo "    -t      Run test ID the number amount of times is recommended"
	echo "    -w      Watch test ID run until it runs into an error"
	echo "    -c      Run test ID once"
	echo "    -s      Run test ID x test-count number of times"
	echo "    -l      List all test ID list"
	echo " -h|--help  Help"
	echo
	echo "If an error every occurs execution will immediately terminate."
	echo "If you are adding a new test try using -w <test-ID> first to"
	echo "make sure the test passes a series of tests."
	echo
	echo Example uses:
	echo
	echo "$TEST_NAME.sh            -- executes all tests"
	echo "$TEST_NAME.sh -t 0002    -- Executes test ID 0002 number of times is recomended"
	echo "$TEST_NAME.sh -w 0002    -- Watch test ID 0002 run until an error occurs"
	echo "$TEST_NAME.sh -s 0002    -- Run test ID 0002 once"
	echo "$TEST_NAME.sh -c 0002 3  -- Run test ID 0002 three times"
	echo
	list_tests
	exit 1
}

function test_num()
{
	re='^[0-9]+$'
	if ! [[ $1 =~ $re ]]; then
		usage
	fi
}

function get_test_count()
{
	test_num $1
	TEST_DATA=$(echo $ALL_TESTS | awk '{print $'$1'}')
	LAST_TWO=${TEST_DATA#*:*}
	echo ${LAST_TWO%:*}
}

function get_test_enabled()
{
	test_num $1
	TEST_DATA=$(echo $ALL_TESTS | awk '{print $'$1'}')
	echo ${TEST_DATA#*:*:}
}

function run_all_tests()
{
	for i in $ALL_TESTS ; do
		TEST_ID=${i%:*:*}
		ENABLED=$(get_test_enabled $TEST_ID)
		TEST_COUNT=$(get_test_count $TEST_ID)
		if [[ $ENABLED -eq "1" ]]; then
			test_case $TEST_ID $TEST_COUNT
		fi
	done
}

function watch_log()
{
	if [ $# -ne 3 ]; then
		clear
	fi
	date
	echo "Running test: $2 - run #$1"
}

function watch_case()
{
	i=0
	while [ 1 ]; do

		if [ $# -eq 1 ]; then
			test_num $1
			watch_log $i ${TEST_NAME}_test_$1
			${TEST_NAME}_test_$1
		else
			watch_log $i all
			run_all_tests
		fi
		let i=$i+1
	done
}

function test_case()
{
	NUM_TESTS=$DEFAULT_NUM_TESTS
	if [ $# -eq 2 ]; then
		NUM_TESTS=$2
	fi

	i=0
	while [ $i -lt $NUM_TESTS ]; do
		test_num $1
		watch_log $i ${TEST_NAME}_test_$1 noclear
		RUN_TEST=${TEST_NAME}_test_$1
		$RUN_TEST
		let i=$i+1
	done
}

function parse_args()
{
	if [ $# -eq 0 ]; then
		run_all_tests
	else
		if [[ "$1" = "all" ]]; then
			run_all_tests
		elif [[ "$1" = "-w" ]]; then
			shift
			watch_case $@
		elif [[ "$1" = "-t" ]]; then
			shift
			test_num $1
			test_case $1 $(get_test_count $1)
		elif [[ "$1" = "-c" ]]; then
			shift
			test_num $1
			test_num $2
			test_case $1 $2
		elif [[ "$1" = "-s" ]]; then
			shift
			test_case $1 1
		elif [[ "$1" = "-l" ]]; then
			list_tests
		elif [[ "$1" = "-h" || "$1" = "--help" ]]; then
			usage
		else
			usage
		fi
	fi
}

test_reqs
allow_user_defaults
check_production_sysctl_writes_strict
load_req_mod

trap "test_finish" EXIT

parse_args $@

exit 0
