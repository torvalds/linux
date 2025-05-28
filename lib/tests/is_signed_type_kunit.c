// SPDX-License-Identifier: GPL-2.0 OR MIT
/*
 *	./tools/testing/kunit/kunit.py run is_signed_type [--raw_output]
 */
#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <kunit/test.h>
#include <linux/compiler.h>

enum unsigned_enum {
	constant_a = 3,
};

enum signed_enum {
	constant_b = -1,
	constant_c = 2,
};

static void is_signed_type_test(struct kunit *test)
{
	KUNIT_EXPECT_EQ(test, is_signed_type(bool), false);
	KUNIT_EXPECT_EQ(test, is_signed_type(signed char), true);
	KUNIT_EXPECT_EQ(test, is_signed_type(unsigned char), false);
	KUNIT_EXPECT_EQ(test, is_signed_type(char), false);
	KUNIT_EXPECT_EQ(test, is_signed_type(int), true);
	KUNIT_EXPECT_EQ(test, is_signed_type(unsigned int), false);
	KUNIT_EXPECT_EQ(test, is_signed_type(long), true);
	KUNIT_EXPECT_EQ(test, is_signed_type(unsigned long), false);
	KUNIT_EXPECT_EQ(test, is_signed_type(long long), true);
	KUNIT_EXPECT_EQ(test, is_signed_type(unsigned long long), false);
	KUNIT_EXPECT_EQ(test, is_signed_type(enum unsigned_enum), false);
	KUNIT_EXPECT_EQ(test, is_signed_type(enum signed_enum), true);
	KUNIT_EXPECT_EQ(test, is_signed_type(void *), false);
	KUNIT_EXPECT_EQ(test, is_signed_type(const char *), false);
}

static struct kunit_case is_signed_type_test_cases[] = {
	KUNIT_CASE(is_signed_type_test),
	{}
};

static struct kunit_suite is_signed_type_test_suite = {
	.name = "is_signed_type",
	.test_cases = is_signed_type_test_cases,
};

kunit_test_suite(is_signed_type_test_suite);

MODULE_DESCRIPTION("is_signed_type() KUnit test suite");
MODULE_LICENSE("Dual MIT/GPL");
