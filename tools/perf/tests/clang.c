// SPDX-License-Identifier: GPL-2.0
#include "tests.h"
#include "c++/clang-c.h"
#include <linux/kernel.h>

#ifndef HAVE_LIBCLANGLLVM_SUPPORT
static int test__clang_to_IR(struct test_suite *test __maybe_unused,
			     int subtest __maybe_unused)
{
	return TEST_SKIP;
}

static int test__clang_to_obj(struct test_suite *test __maybe_unused,
			      int subtest __maybe_unused)
{
	return TEST_SKIP;
}
#endif

static struct test_case clang_tests[] = {
	TEST_CASE_REASON("builtin clang compile C source to IR", clang_to_IR,
			 "not compiled in"),
	TEST_CASE_REASON("builtin clang compile C source to ELF object",
			 clang_to_obj,
			 "not compiled in"),
	{ .name = NULL, }
};

struct test_suite suite__clang = {
	.desc = "builtin clang support",
	.test_cases = clang_tests,
};
