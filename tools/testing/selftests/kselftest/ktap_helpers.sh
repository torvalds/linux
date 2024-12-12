# SPDX-License-Identifier: GPL-2.0
#
# Copyright (c) 2023 Collabora Ltd
#
# Helpers for outputting in KTAP format
#
KTAP_TESTNO=1
KTAP_CNT_PASS=0
KTAP_CNT_FAIL=0
KTAP_CNT_SKIP=0

KSFT_PASS=0
KSFT_FAIL=1
KSFT_XFAIL=2
KSFT_XPASS=3
KSFT_SKIP=4

KSFT_NUM_TESTS=0

ktap_print_header() {
	echo "TAP version 13"
}

ktap_print_msg()
{
	echo "#" $@
}

ktap_set_plan() {
	KSFT_NUM_TESTS="$1"

	echo "1..$KSFT_NUM_TESTS"
}

ktap_skip_all() {
	echo -n "1..0 # SKIP "
	echo $@
}

__ktap_test() {
	result="$1"
	description="$2"
	directive="${3:-}" # optional

	local directive_str=
	[ ! -z "$directive" ] && directive_str="# $directive"

	echo $result $KTAP_TESTNO $description $directive_str

	KTAP_TESTNO=$((KTAP_TESTNO+1))
}

ktap_test_pass() {
	description="$1"

	result="ok"
	__ktap_test "$result" "$description"

	KTAP_CNT_PASS=$((KTAP_CNT_PASS+1))
}

ktap_test_skip() {
	description="$1"

	result="ok"
	directive="SKIP"
	__ktap_test "$result" "$description" "$directive"

	KTAP_CNT_SKIP=$((KTAP_CNT_SKIP+1))
}

ktap_test_fail() {
	description="$1"

	result="not ok"
	__ktap_test "$result" "$description"

	KTAP_CNT_FAIL=$((KTAP_CNT_FAIL+1))
}

ktap_test_result() {
	description="$1"
	shift

	if $@; then
		ktap_test_pass "$description"
	else
		ktap_test_fail "$description"
	fi
}

ktap_exit_fail_msg() {
	echo "Bail out! " $@
	ktap_print_totals

	exit "$KSFT_FAIL"
}

ktap_finished() {
	ktap_print_totals

	if [ $((KTAP_CNT_PASS + KTAP_CNT_SKIP)) -eq "$KSFT_NUM_TESTS" ]; then
		exit "$KSFT_PASS"
	else
		exit "$KSFT_FAIL"
	fi
}

ktap_print_totals() {
	echo "# Totals: pass:$KTAP_CNT_PASS fail:$KTAP_CNT_FAIL xfail:0 xpass:0 skip:$KTAP_CNT_SKIP error:0"
}
