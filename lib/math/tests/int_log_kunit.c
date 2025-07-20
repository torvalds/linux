// SPDX-License-Identifier: GPL-2.0-only
#include <kunit/test.h>
#include <linux/int_log.h>

struct test_case_params {
	u32 value;
	unsigned int expected_result;
	const char *name;
};


/* The expected result takes into account the log error */
static const struct test_case_params intlog2_params[] = {
	{0, 0, "Log base 2 of 0"},
	{1, 0, "Log base 2 of 1"},
	{2, 16777216, "Log base 2 of 2"},
	{3, 26591232, "Log base 2 of 3"},
	{4, 33554432, "Log base 2 of 4"},
	{8, 50331648, "Log base 2 of 8"},
	{16, 67108864, "Log base 2 of 16"},
	{32, 83886080, "Log base 2 of 32"},
	{U32_MAX, 536870911, "Log base 2 of MAX"},
};

static const struct test_case_params intlog10_params[] = {
	{0, 0, "Log base 10 of 0"},
	{1, 0, "Log base 10 of 1"},
	{6, 13055203, "Log base 10 of 6"},
	{10, 16777225, "Log base 10 of 10"},
	{100, 33554450, "Log base 10 of 100"},
	{1000, 50331675, "Log base 10 of 1000"},
	{10000, 67108862, "Log base 10 of 10000"},
	{U32_MAX, 161614247, "Log base 10 of MAX"}
};

static void get_desc(const struct test_case_params *tc, char *desc)
{
	strscpy(desc, tc->name, KUNIT_PARAM_DESC_SIZE);
}


KUNIT_ARRAY_PARAM(intlog2, intlog2_params, get_desc);

static void intlog2_test(struct kunit *test)
{
	const struct test_case_params *tc = (const struct test_case_params *)test->param_value;

	KUNIT_EXPECT_EQ(test, tc->expected_result, intlog2(tc->value));
}

KUNIT_ARRAY_PARAM(intlog10, intlog10_params, get_desc);

static void intlog10_test(struct kunit *test)
{
	const struct test_case_params *tc = (const struct test_case_params *)test->param_value;

	KUNIT_EXPECT_EQ(test, tc->expected_result, intlog10(tc->value));
}

static struct kunit_case math_int_log_test_cases[] = {
	KUNIT_CASE_PARAM(intlog2_test, intlog2_gen_params),
	KUNIT_CASE_PARAM(intlog10_test, intlog10_gen_params),
	{}
};

static struct kunit_suite int_log_test_suite = {
	.name = "math-int_log",
	.test_cases =  math_int_log_test_cases,
};

kunit_test_suites(&int_log_test_suite);

MODULE_DESCRIPTION("math.int_log KUnit test suite");
MODULE_LICENSE("GPL");
