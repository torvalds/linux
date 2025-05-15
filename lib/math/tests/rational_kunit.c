// SPDX-License-Identifier: GPL-2.0

#include <kunit/test.h>

#include <linux/rational.h>

struct rational_test_param {
	unsigned long num, den;
	unsigned long max_num, max_den;
	unsigned long exp_num, exp_den;

	const char *name;
};

static const struct rational_test_param test_parameters[] = {
	{ 1230,	10,	100, 20,	100, 1,    "Exceeds bounds, semi-convergent term > 1/2 last term" },
	{ 34567,100, 	120, 20,	120, 1,    "Exceeds bounds, semi-convergent term < 1/2 last term" },
	{ 1, 30,	100, 10,	0, 1,	   "Closest to zero" },
	{ 1, 19,	100, 10,	1, 10,     "Closest to smallest non-zero" },
	{ 27,32,	16, 16,		11, 13,    "Use convergent" },
	{ 1155, 7735,	255, 255,	33, 221,   "Exact answer" },
	{ 87, 32,	70, 32,		68, 25,    "Semiconvergent, numerator limit" },
	{ 14533, 4626,	15000, 2400,	7433, 2366, "Semiconvergent, denominator limit" },
};

static void get_desc(const struct rational_test_param *param, char *desc)
{
	strscpy(desc, param->name, KUNIT_PARAM_DESC_SIZE);
}

/* Creates function rational_gen_params */
KUNIT_ARRAY_PARAM(rational, test_parameters, get_desc);

static void rational_test(struct kunit *test)
{
	const struct rational_test_param *param = (const struct rational_test_param *)test->param_value;
	unsigned long n = 0, d = 0;

	rational_best_approximation(param->num, param->den, param->max_num, param->max_den, &n, &d);
	KUNIT_EXPECT_EQ(test, n, param->exp_num);
	KUNIT_EXPECT_EQ(test, d, param->exp_den);
}

static struct kunit_case rational_test_cases[] = {
	KUNIT_CASE_PARAM(rational_test, rational_gen_params),
	{}
};

static struct kunit_suite rational_test_suite = {
	.name = "rational",
	.test_cases = rational_test_cases,
};

kunit_test_suites(&rational_test_suite);

MODULE_DESCRIPTION("Rational fractions unit test");
MODULE_LICENSE("GPL v2");
