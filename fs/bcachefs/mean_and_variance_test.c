// SPDX-License-Identifier: GPL-2.0
#include <kunit/test.h>

#include "mean_and_variance.h"

#define MAX_SQR (SQRT_U64_MAX*SQRT_U64_MAX)

static void mean_and_variance_basic_test(struct kunit *test)
{
	struct mean_and_variance s = {};

	s = mean_and_variance_update(s, 2);
	s = mean_and_variance_update(s, 2);

	KUNIT_EXPECT_EQ(test, mean_and_variance_get_mean(s), 2);
	KUNIT_EXPECT_EQ(test, mean_and_variance_get_variance(s), 0);
	KUNIT_EXPECT_EQ(test, s.n, 2);

	s = mean_and_variance_update(s, 4);
	s = mean_and_variance_update(s, 4);

	KUNIT_EXPECT_EQ(test, mean_and_variance_get_mean(s), 3);
	KUNIT_EXPECT_EQ(test, mean_and_variance_get_variance(s), 1);
	KUNIT_EXPECT_EQ(test, s.n, 4);
}

/*
 * Test values computed using a spreadsheet from the psuedocode at the bottom:
 * https://fanf2.user.srcf.net/hermes/doc/antiforgery/stats.pdf
 */

static void mean_and_variance_weighted_test(struct kunit *test)
{
	struct mean_and_variance_weighted s = { .weight = 2 };

	s.weight = 2;

	mean_and_variance_weighted_update(&s, 10);
	KUNIT_EXPECT_EQ(test, mean_and_variance_weighted_get_mean(s), 10);
	KUNIT_EXPECT_EQ(test, mean_and_variance_weighted_get_variance(s), 0);

	mean_and_variance_weighted_update(&s, 20);
	KUNIT_EXPECT_EQ(test, mean_and_variance_weighted_get_mean(s), 12);
	KUNIT_EXPECT_EQ(test, mean_and_variance_weighted_get_variance(s), 18);

	mean_and_variance_weighted_update(&s, 30);
	KUNIT_EXPECT_EQ(test, mean_and_variance_weighted_get_mean(s), 16);
	KUNIT_EXPECT_EQ(test, mean_and_variance_weighted_get_variance(s), 72);

	s = (struct mean_and_variance_weighted) { .weight = 2 };

	mean_and_variance_weighted_update(&s, -10);
	KUNIT_EXPECT_EQ(test, mean_and_variance_weighted_get_mean(s), -10);
	KUNIT_EXPECT_EQ(test, mean_and_variance_weighted_get_variance(s), 0);

	mean_and_variance_weighted_update(&s, -20);
	KUNIT_EXPECT_EQ(test, mean_and_variance_weighted_get_mean(s), -12);
	KUNIT_EXPECT_EQ(test, mean_and_variance_weighted_get_variance(s), 18);

	mean_and_variance_weighted_update(&s, -30);
	KUNIT_EXPECT_EQ(test, mean_and_variance_weighted_get_mean(s), -16);
	KUNIT_EXPECT_EQ(test, mean_and_variance_weighted_get_variance(s), 72);

}

static void mean_and_variance_weighted_advanced_test(struct kunit *test)
{
	struct mean_and_variance_weighted s = { .weight = 8 };
	s64 i;

	for (i = 10; i <= 100; i += 10)
		mean_and_variance_weighted_update(&s, i);

	KUNIT_EXPECT_EQ(test, mean_and_variance_weighted_get_mean(s), 11);
	KUNIT_EXPECT_EQ(test, mean_and_variance_weighted_get_variance(s), 107);

	s = (struct mean_and_variance_weighted) { .weight = 8 };

	for (i = -10; i >= -100; i -= 10)
		mean_and_variance_weighted_update(&s, i);

	KUNIT_EXPECT_EQ(test, mean_and_variance_weighted_get_mean(s), -11);
	KUNIT_EXPECT_EQ(test, mean_and_variance_weighted_get_variance(s), 107);

}

static void mean_and_variance_fast_divpow2(struct kunit *test)
{
	s64 i;
	u8 d;

	for (i = 0; i < 100; i++) {
		d = 0;
		KUNIT_EXPECT_EQ(test, fast_divpow2(i, d), div_u64(i, 1LLU << d));
		KUNIT_EXPECT_EQ(test, abs(fast_divpow2(-i, d)), div_u64(i, 1LLU << d));
		for (d = 1; d < 32; d++) {
			KUNIT_EXPECT_EQ_MSG(test, abs(fast_divpow2(i, d)),
					    div_u64(i, 1 << d), "%lld %u", i, d);
			KUNIT_EXPECT_EQ_MSG(test, abs(fast_divpow2(-i, d)),
					    div_u64(i, 1 << d), "%lld %u", -i, d);
		}
	}
}

static void mean_and_variance_u128_basic_test(struct kunit *test)
{
	u128_u a  = u64s_to_u128(0, U64_MAX);
	u128_u a1 = u64s_to_u128(0, 1);
	u128_u b  = u64s_to_u128(1, 0);
	u128_u c  = u64s_to_u128(0, 1LLU << 63);
	u128_u c2 = u64s_to_u128(U64_MAX, U64_MAX);

	KUNIT_EXPECT_EQ(test, u128_hi(u128_add(a, a1)), 1);
	KUNIT_EXPECT_EQ(test, u128_lo(u128_add(a, a1)), 0);
	KUNIT_EXPECT_EQ(test, u128_hi(u128_add(a1, a)), 1);
	KUNIT_EXPECT_EQ(test, u128_lo(u128_add(a1, a)), 0);

	KUNIT_EXPECT_EQ(test, u128_lo(u128_sub(b, a1)), U64_MAX);
	KUNIT_EXPECT_EQ(test, u128_hi(u128_sub(b, a1)), 0);

	KUNIT_EXPECT_EQ(test, u128_hi(u128_shl(c, 1)), 1);
	KUNIT_EXPECT_EQ(test, u128_lo(u128_shl(c, 1)), 0);

	KUNIT_EXPECT_EQ(test, u128_hi(u128_square(U64_MAX)), U64_MAX - 1);
	KUNIT_EXPECT_EQ(test, u128_lo(u128_square(U64_MAX)), 1);

	KUNIT_EXPECT_EQ(test, u128_lo(u128_div(b, 2)), 1LLU << 63);

	KUNIT_EXPECT_EQ(test, u128_hi(u128_div(c2, 2)), U64_MAX >> 1);
	KUNIT_EXPECT_EQ(test, u128_lo(u128_div(c2, 2)), U64_MAX);

	KUNIT_EXPECT_EQ(test, u128_hi(u128_div(u128_shl(u64_to_u128(U64_MAX), 32), 2)), U32_MAX >> 1);
	KUNIT_EXPECT_EQ(test, u128_lo(u128_div(u128_shl(u64_to_u128(U64_MAX), 32), 2)), U64_MAX << 31);
}

static struct kunit_case mean_and_variance_test_cases[] = {
	KUNIT_CASE(mean_and_variance_fast_divpow2),
	KUNIT_CASE(mean_and_variance_u128_basic_test),
	KUNIT_CASE(mean_and_variance_basic_test),
	KUNIT_CASE(mean_and_variance_weighted_test),
	KUNIT_CASE(mean_and_variance_weighted_advanced_test),
	{}
};

static struct kunit_suite mean_and_variance_test_suite = {
	.name		= "mean and variance tests",
	.test_cases	= mean_and_variance_test_cases
};

kunit_test_suite(mean_and_variance_test_suite);

MODULE_AUTHOR("Daniel B. Hill");
MODULE_LICENSE("GPL");
