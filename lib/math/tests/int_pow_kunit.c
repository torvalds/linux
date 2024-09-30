// SPDX-License-Identifier: GPL-2.0-only

#include <kunit/test.h>
#include <linux/math.h>

struct test_case_params {
	u64 base;
	unsigned int exponent;
	u64 expected_result;
	const char *name;
};

static const struct test_case_params params[] = {
	{ 64, 0, 1, "Power of zero" },
	{ 64, 1, 64, "Power of one"},
	{ 0, 5, 0, "Base zero" },
	{ 1, 64, 1, "Base one" },
	{ 2, 2, 4, "Two squared"},
	{ 2, 3, 8, "Two cubed"},
	{ 5, 5, 3125, "Five raised to the fifth power" },
	{ U64_MAX, 1, U64_MAX, "Max base" },
	{ 2, 63, 9223372036854775808ULL, "Large result"},
};

static void get_desc(const struct test_case_params *tc, char *desc)
{
	strscpy(desc, tc->name, KUNIT_PARAM_DESC_SIZE);
}

KUNIT_ARRAY_PARAM(int_pow, params, get_desc);

static void int_pow_test(struct kunit *test)
{
	const struct test_case_params *tc = (const struct test_case_params *)test->param_value;

	KUNIT_EXPECT_EQ(test, tc->expected_result, int_pow(tc->base, tc->exponent));
}

static struct kunit_case math_int_pow_test_cases[] = {
	KUNIT_CASE_PARAM(int_pow_test, int_pow_gen_params),
	{}
};

static struct kunit_suite int_pow_test_suite = {
	.name = "math-int_pow",
	.test_cases = math_int_pow_test_cases,
};

kunit_test_suites(&int_pow_test_suite);

MODULE_DESCRIPTION("math.int_pow KUnit test suite");
MODULE_LICENSE("GPL");
