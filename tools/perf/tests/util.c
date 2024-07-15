// SPDX-License-Identifier: GPL-2.0
#include "tests.h"
#include "util/debug.h"

#include <linux/compiler.h>
#include <stdlib.h>
#include <string2.h>

static int test_strreplace(char needle, const char *haystack,
			   const char *replace, const char *expected)
{
	char *new = strreplace_chars(needle, haystack, replace);
	int ret = strcmp(new, expected);

	free(new);
	return ret == 0;
}

static int test__util(struct test_suite *t __maybe_unused, int subtest __maybe_unused)
{
	TEST_ASSERT_VAL("empty string", test_strreplace(' ', "", "123", ""));
	TEST_ASSERT_VAL("no match", test_strreplace('5', "123", "4", "123"));
	TEST_ASSERT_VAL("replace 1", test_strreplace('3', "123", "4", "124"));
	TEST_ASSERT_VAL("replace 2", test_strreplace('a', "abcabc", "ef", "efbcefbc"));
	TEST_ASSERT_VAL("replace long", test_strreplace('a', "abcabc", "longlong",
							"longlongbclonglongbc"));

	return 0;
}

DEFINE_SUITE("util", util);
