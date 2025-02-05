// SPDX-License-Identifier: GPL-2.0+
/*
 * Test cases for functions and macros in bits.h
 */

#include <kunit/test.h>
#include <linux/bits.h>


static void genmask_test(struct kunit *test)
{
	KUNIT_EXPECT_EQ(test, 1ul, GENMASK(0, 0));
	KUNIT_EXPECT_EQ(test, 3ul, GENMASK(1, 0));
	KUNIT_EXPECT_EQ(test, 6ul, GENMASK(2, 1));
	KUNIT_EXPECT_EQ(test, 0xFFFFFFFFul, GENMASK(31, 0));

#ifdef TEST_GENMASK_FAILURES
	/* these should fail compilation */
	GENMASK(0, 1);
	GENMASK(0, 10);
	GENMASK(9, 10);
#endif


}

static void genmask_ull_test(struct kunit *test)
{
	KUNIT_EXPECT_EQ(test, 1ull, GENMASK_ULL(0, 0));
	KUNIT_EXPECT_EQ(test, 3ull, GENMASK_ULL(1, 0));
	KUNIT_EXPECT_EQ(test, 0x000000ffffe00000ull, GENMASK_ULL(39, 21));
	KUNIT_EXPECT_EQ(test, 0xffffffffffffffffull, GENMASK_ULL(63, 0));

#ifdef TEST_GENMASK_FAILURES
	/* these should fail compilation */
	GENMASK_ULL(0, 1);
	GENMASK_ULL(0, 10);
	GENMASK_ULL(9, 10);
#endif
}

static void genmask_u128_test(struct kunit *test)
{
#ifdef CONFIG_ARCH_SUPPORTS_INT128
	/* Below 64 bit masks */
	KUNIT_EXPECT_EQ(test, 0x0000000000000001ull, GENMASK_U128(0, 0));
	KUNIT_EXPECT_EQ(test, 0x0000000000000003ull, GENMASK_U128(1, 0));
	KUNIT_EXPECT_EQ(test, 0x0000000000000006ull, GENMASK_U128(2, 1));
	KUNIT_EXPECT_EQ(test, 0x00000000ffffffffull, GENMASK_U128(31, 0));
	KUNIT_EXPECT_EQ(test, 0x000000ffffe00000ull, GENMASK_U128(39, 21));
	KUNIT_EXPECT_EQ(test, 0xffffffffffffffffull, GENMASK_U128(63, 0));

	/* Above 64 bit masks - only 64 bit portion can be validated once */
	KUNIT_EXPECT_EQ(test, 0xffffffffffffffffull, GENMASK_U128(64, 0) >> 1);
	KUNIT_EXPECT_EQ(test, 0x00000000ffffffffull, GENMASK_U128(81, 50) >> 50);
	KUNIT_EXPECT_EQ(test, 0x0000000000ffffffull, GENMASK_U128(87, 64) >> 64);
	KUNIT_EXPECT_EQ(test, 0x0000000000ff0000ull, GENMASK_U128(87, 80) >> 64);

	KUNIT_EXPECT_EQ(test, 0xffffffffffffffffull, GENMASK_U128(127, 0) >> 64);
	KUNIT_EXPECT_EQ(test, 0xffffffffffffffffull, (u64)GENMASK_U128(127, 0));
	KUNIT_EXPECT_EQ(test, 0x0000000000000003ull, GENMASK_U128(127, 126) >> 126);
	KUNIT_EXPECT_EQ(test, 0x0000000000000001ull, GENMASK_U128(127, 127) >> 127);
#ifdef TEST_GENMASK_FAILURES
	/* these should fail compilation */
	GENMASK_U128(0, 1);
	GENMASK_U128(0, 10);
	GENMASK_U128(9, 10);
#endif /* TEST_GENMASK_FAILURES */
#endif /* CONFIG_ARCH_SUPPORTS_INT128 */
}

static void genmask_input_check_test(struct kunit *test)
{
	unsigned int x, y;
	int z, w;

	/* Unknown input */
	KUNIT_EXPECT_EQ(test, 0, GENMASK_INPUT_CHECK(x, 0));
	KUNIT_EXPECT_EQ(test, 0, GENMASK_INPUT_CHECK(0, x));
	KUNIT_EXPECT_EQ(test, 0, GENMASK_INPUT_CHECK(x, y));

	KUNIT_EXPECT_EQ(test, 0, GENMASK_INPUT_CHECK(z, 0));
	KUNIT_EXPECT_EQ(test, 0, GENMASK_INPUT_CHECK(0, z));
	KUNIT_EXPECT_EQ(test, 0, GENMASK_INPUT_CHECK(z, w));

	/* Valid input */
	KUNIT_EXPECT_EQ(test, 0, GENMASK_INPUT_CHECK(1, 1));
	KUNIT_EXPECT_EQ(test, 0, GENMASK_INPUT_CHECK(39, 21));
	KUNIT_EXPECT_EQ(test, 0, GENMASK_INPUT_CHECK(100, 80));
	KUNIT_EXPECT_EQ(test, 0, GENMASK_INPUT_CHECK(110, 65));
	KUNIT_EXPECT_EQ(test, 0, GENMASK_INPUT_CHECK(127, 0));
}


static struct kunit_case bits_test_cases[] = {
	KUNIT_CASE(genmask_test),
	KUNIT_CASE(genmask_ull_test),
	KUNIT_CASE(genmask_u128_test),
	KUNIT_CASE(genmask_input_check_test),
	{}
};

static struct kunit_suite bits_test_suite = {
	.name = "bits-test",
	.test_cases = bits_test_cases,
};
kunit_test_suite(bits_test_suite);

MODULE_DESCRIPTION("Test cases for functions and macros in bits.h");
MODULE_LICENSE("GPL");
