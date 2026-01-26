#!/bin/bash
# SPDX-License-Identifier: GPL-2.0

ALL_TESTS="
	settime
	adjtime
	adjfreq
"
DEV=$1

# Kselftest framework requirement - SKIP code is 4.
ksft_skip=4

##############################################################################
# Sanity checks

if [[ "$(id -u)" -ne 0 ]]; then
	echo "SKIP: need root privileges"
	exit $ksft_skip
fi

if [[ "$DEV" == "" ]]; then
	echo "SKIP: PTP device not provided"
	exit $ksft_skip
fi

require_command()
{
	local cmd=$1; shift

	if [[ ! -x "$(command -v "$cmd")" ]]; then
		echo "SKIP: $cmd not installed"
		exit $ksft_skip
	fi
}

phc_sanity()
{
	phc_ctl $DEV get &> /dev/null

	if [ $? != 0 ]; then
		echo "SKIP: unknown clock $DEV: No such device"
		exit $ksft_skip
	fi
}

require_command phc_ctl
phc_sanity

##############################################################################
# Helpers

# Exit status to return at the end. Set in case one of the tests fails.
EXIT_STATUS=0
PASS_COUNT=0
# Per-test return value. Clear at the beginning of each test.
RET=0

check_err()
{
	local err=$1

	if [[ $RET -eq 0 && $err -ne 0 ]]; then
		RET=$err
	fi
}

log_test()
{
	local test_name=$1

	if [[ $RET -eq $ksft_skip ]]; then
		printf "TEST: %-60s  [SKIP]\n" "$test_name"
		return 0
	fi

	if [[ $RET -ne 0 ]]; then
		EXIT_STATUS=1
		printf "TEST: %-60s  [FAIL]\n" "$test_name"
		return 1
	fi

	((PASS_COUNT++))
	printf "TEST: %-60s  [ OK ]\n" "$test_name"
	return 0
}

tests_run()
{
	local current_test

	for current_test in ${TESTS:-$ALL_TESTS}; do
		$current_test
	done
}

##############################################################################
# Tests

settime_do()
{
	local res out

	out=$(LC_ALL=C phc_ctl $DEV set 0 wait 120.5 get 2>&1)
	if [[ $? -ne 0 ]]; then
		if echo "$out" | grep -qi "Operation not supported"; then
			return $ksft_skip
		fi
		return 1
	fi
	res=$(echo "$out" | awk '/clock time is/{print $5}' | awk -F. '{print $1}')

	(( res == 120 ))
}

adjtime_do()
{
	local res out

	out=$(LC_ALL=C phc_ctl $DEV set 0 adj 10 get 2>&1)
	if [[ $? -ne 0 ]]; then
		if echo "$out" | grep -qi "Operation not supported"; then
			return $ksft_skip
		fi
		return 1
	fi
	res=$(echo "$out" | awk '/clock time is/{print $5}' | awk -F. '{print $1}')

	(( res == 10 ))
}

adjfreq_do()
{
	local res out

	# Set the clock to be 1% faster
	out=$(LC_ALL=C phc_ctl $DEV freq 10000000 set 0 wait 100.5 get 2>&1)
	if [[ $? -ne 0 ]]; then
		if echo "$out" | grep -qi "Operation not supported"; then
			return $ksft_skip
		fi
		return 1
	fi
	res=$(echo "$out" | awk '/clock time is/{print $5}' | awk -F. '{print $1}')

	(( res == 101 ))
}

##############################################################################

cleanup()
{
	phc_ctl $DEV freq 0.0 &> /dev/null
	phc_ctl $DEV set &> /dev/null
}

settime()
{
	RET=0

	settime_do
	check_err $?
	log_test "settime"
	cleanup
}

adjtime()
{
	RET=0

	adjtime_do
	check_err $?
	log_test "adjtime"
	cleanup
}

adjfreq()
{
	RET=0

	adjfreq_do
	check_err $?
	log_test "adjfreq"
	cleanup
}

trap cleanup EXIT

tests_run

if [[ $EXIT_STATUS -eq 0 && $PASS_COUNT -eq 0 ]]; then
	exit $ksft_skip
fi
exit $EXIT_STATUS
