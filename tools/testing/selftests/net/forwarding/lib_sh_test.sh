#!/bin/bash
# SPDX-License-Identifier: GPL-2.0

# This tests the operation of lib.sh itself.

ALL_TESTS="
	test_ret
	test_exit_status
"
NUM_NETIFS=0
source lib.sh

# Simulated checks.

do_test()
{
	local msg=$1; shift

	"$@"
	check_err $? "$msg"
}

tpass()
{
	do_test "tpass" true
}

tfail()
{
	do_test "tfail" false
}

txfail()
{
	FAIL_TO_XFAIL=yes do_test "txfail" false
}

# Simulated tests.

pass()
{
	RET=0
	do_test "true" true
	log_test "true"
}

fail()
{
	RET=0
	do_test "false" false
	log_test "false"
}

xfail()
{
	RET=0
	FAIL_TO_XFAIL=yes do_test "xfalse" false
	log_test "xfalse"
}

skip()
{
	RET=0
	log_test_skip "skip"
}

slow_xfail()
{
	RET=0
	xfail_on_slow do_test "slow_false" false
	log_test "slow_false"
}

# lib.sh tests.

ret_tests_run()
{
	local t

	RET=0
	retmsg=
	for t in "$@"; do
		$t
	done
	echo "$retmsg"
	return $RET
}

ret_subtest()
{
	local expect_ret=$1; shift
	local expect_retmsg=$1; shift
	local -a tests=( "$@" )

	local status_names=(pass fail xfail xpass skip)
	local ret
	local out

	RET=0

	# Run this in a subshell, so that our environment is intact.
	out=$(ret_tests_run "${tests[@]}")
	ret=$?

	(( ret == expect_ret ))
	check_err $? "RET=$ret expected $expect_ret"

	[[ $out == $expect_retmsg ]]
	check_err $? "retmsg=$out expected $expect_retmsg"

	log_test "RET $(echo ${tests[@]}) -> ${status_names[$ret]}"
}

test_ret()
{
	ret_subtest $ksft_pass ""

	ret_subtest $ksft_pass "" tpass
	ret_subtest $ksft_fail "tfail" tfail
	ret_subtest $ksft_xfail "txfail" txfail

	ret_subtest $ksft_pass "" tpass tpass
	ret_subtest $ksft_fail "tfail" tpass tfail
	ret_subtest $ksft_xfail "txfail" tpass txfail

	ret_subtest $ksft_fail "tfail" tfail tpass
	ret_subtest $ksft_xfail "txfail" txfail tpass

	ret_subtest $ksft_fail "tfail" tfail tfail
	ret_subtest $ksft_fail "tfail" tfail txfail

	ret_subtest $ksft_fail "tfail" txfail tfail

	ret_subtest $ksft_xfail "txfail" txfail txfail
}

exit_status_tests_run()
{
	EXIT_STATUS=0
	tests_run > /dev/null
	return $EXIT_STATUS
}

exit_status_subtest()
{
	local expect_exit_status=$1; shift
	local tests=$1; shift
	local what=$1; shift

	local status_names=(pass fail xfail xpass skip)
	local exit_status
	local out

	RET=0

	# Run this in a subshell, so that our environment is intact.
	out=$(TESTS="$tests" exit_status_tests_run)
	exit_status=$?

	(( exit_status == expect_exit_status ))
	check_err $? "EXIT_STATUS=$exit_status, expected $expect_exit_status"

	log_test "EXIT_STATUS $tests$what -> ${status_names[$exit_status]}"
}

test_exit_status()
{
	exit_status_subtest $ksft_pass ":"

	exit_status_subtest $ksft_pass "pass"
	exit_status_subtest $ksft_fail "fail"
	exit_status_subtest $ksft_pass "xfail"
	exit_status_subtest $ksft_skip "skip"

	exit_status_subtest $ksft_pass "pass pass"
	exit_status_subtest $ksft_fail "pass fail"
	exit_status_subtest $ksft_pass "pass xfail"
	exit_status_subtest $ksft_skip "pass skip"

	exit_status_subtest $ksft_fail "fail pass"
	exit_status_subtest $ksft_pass "xfail pass"
	exit_status_subtest $ksft_skip "skip pass"

	exit_status_subtest $ksft_fail "fail fail"
	exit_status_subtest $ksft_fail "fail xfail"
	exit_status_subtest $ksft_fail "fail skip"

	exit_status_subtest $ksft_fail "xfail fail"
	exit_status_subtest $ksft_fail "skip fail"

	exit_status_subtest $ksft_pass "xfail xfail"
	exit_status_subtest $ksft_skip "xfail skip"
	exit_status_subtest $ksft_skip "skip xfail"

	exit_status_subtest $ksft_skip "skip skip"

	KSFT_MACHINE_SLOW=yes \
		exit_status_subtest $ksft_pass "slow_xfail" ": slow"

	KSFT_MACHINE_SLOW=no \
		exit_status_subtest $ksft_fail "slow_xfail" ": fast"
}

trap pre_cleanup EXIT

tests_run

exit $EXIT_STATUS
