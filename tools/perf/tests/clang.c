// SPDX-License-Identifier: GPL-2.0
#include "tests.h"
#include "c++/clang-c.h"
#include <linux/kernel.h>

static struct {
	int (*func)(void);
	const char *desc;
} clang_testcase_table[] = {
#ifdef HAVE_LIBCLANGLLVM_SUPPORT
	{
		.func = test__clang_to_IR,
		.desc = "builtin clang compile C source to IR",
	},
	{
		.func = test__clang_to_obj,
		.desc = "builtin clang compile C source to ELF object",
	},
#endif
};

int test__clang_subtest_get_nr(void)
{
	return (int)ARRAY_SIZE(clang_testcase_table);
}

const char *test__clang_subtest_get_desc(int i)
{
	if (i < 0 || i >= (int)ARRAY_SIZE(clang_testcase_table))
		return NULL;
	return clang_testcase_table[i].desc;
}

#ifndef HAVE_LIBCLANGLLVM_SUPPORT
int test__clang(struct test *test __maybe_unused, int i __maybe_unused)
{
	return TEST_SKIP;
}
#else
int test__clang(struct test *test __maybe_unused, int i)
{
	if (i < 0 || i >= (int)ARRAY_SIZE(clang_testcase_table))
		return TEST_FAIL;
	return clang_testcase_table[i].func();
}
#endif
