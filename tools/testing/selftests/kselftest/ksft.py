# SPDX-License-Identifier: GPL-2.0
#
# Copyright (c) 2023 Collabora Ltd
#
# Kselftest helpers for outputting in KTAP format. Based on kselftest.h.
#

import sys

ksft_cnt = {"pass": 0, "fail": 0, "skip": 0}
ksft_num_tests = 0
ksft_test_number = 1

KSFT_PASS = 0
KSFT_FAIL = 1
KSFT_SKIP = 4


def print_header():
    print("TAP version 13")


def set_plan(num_tests):
    global ksft_num_tests
    ksft_num_tests = num_tests
    print("1..{}".format(num_tests))


def print_cnts():
    print(
        f"# Totals: pass:{ksft_cnt['pass']} fail:{ksft_cnt['fail']} xfail:0 xpass:0 skip:{ksft_cnt['skip']} error:0"
    )


def print_msg(msg):
    print(f"# {msg}")


def _test_print(result, description, directive=None):
    if directive:
        directive_str = f"# {directive}"
    else:
        directive_str = ""

    global ksft_test_number
    print(f"{result} {ksft_test_number} {description} {directive_str}")
    ksft_test_number += 1


def test_result_pass(description):
    _test_print("ok", description)
    ksft_cnt["pass"] += 1


def test_result_fail(description):
    _test_print("not ok", description)
    ksft_cnt["fail"] += 1


def test_result_skip(description):
    _test_print("ok", description, "SKIP")
    ksft_cnt["skip"] += 1


def test_result(condition, description=""):
    if condition:
        test_result_pass(description)
    else:
        test_result_fail(description)


def finished():
    if ksft_cnt["pass"] + ksft_cnt["skip"] == ksft_num_tests:
        exit_code = KSFT_PASS
    else:
        exit_code = KSFT_FAIL

    print_cnts()

    sys.exit(exit_code)


def exit_fail():
    print_cnts()
    sys.exit(KSFT_FAIL)


def exit_pass():
    print_cnts()
    sys.exit(KSFT_PASS)
