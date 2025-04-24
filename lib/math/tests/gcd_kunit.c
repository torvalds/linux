// SPDX-License-Identifier: GPL-2.0-only

#include <kunit/test.h>
#include <linux/gcd.h>
#include <linux/limits.h>

struct test_case_params {
	unsigned long val1;
	unsigned long val2;
	unsigned long expected_result;
	const char *name;
};

static const struct test_case_params params[] = {
	{ 48, 18, 6, "GCD of 48 and 18" },
	{ 18, 48, 6, "GCD of 18 and 48" },
	{ 56, 98, 14, "GCD of 56 and 98" },
	{ 17, 13, 1, "Coprime numbers" },
	{ 101, 103, 1, "Coprime numbers" },
	{ 270, 192, 6, "GCD of 270 and 192" },
	{ 0, 5, 5, "GCD with zero" },
	{ 7, 0, 7, "GCD with zero reversed" },
	{ 36, 36, 36, "GCD of identical numbers" },
	{ ULONG_MAX, 1, 1, "GCD of max ulong and 1" },
	{ ULONG_MAX, ULONG_MAX, ULONG_MAX, "GCD of max ulong values" },
};

static void get_desc(const struct test_case_params *tc, char *desc)
{
	strscpy(desc, tc->name, KUNIT_PARAM_DESC_SIZE);
}

KUNIT_ARRAY_PARAM(gcd, params, get_desc);

static void gcd_test(struct kunit *test)
{
	const struct test_case_params *tc = (const struct test_case_params *)test->param_value;

	KUNIT_EXPECT_EQ(test, tc->expected_result, gcd(tc->val1, tc->val2));
}

static struct kunit_case math_gcd_test_cases[] = {
	KUNIT_CASE_PARAM(gcd_test, gcd_gen_params),
	{}
};

static struct kunit_suite gcd_test_suite = {
	.name = "math-gcd",
	.test_cases = math_gcd_test_cases,
};

kunit_test_suite(gcd_test_suite);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("math.gcd KUnit test suite");
MODULE_AUTHOR("Yu-Chun Lin <eleanor15x@gmail.com>");
