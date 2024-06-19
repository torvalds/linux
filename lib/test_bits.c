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
}


static struct kunit_case bits_test_cases[] = {
	KUNIT_CASE(genmask_test),
	KUNIT_CASE(genmask_ull_test),
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
