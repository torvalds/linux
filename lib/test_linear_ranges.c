// SPDX-License-Identifier: GPL-2.0
/*
 * KUnit test for the linear_ranges helper.
 *
 * Copyright (C) 2020, ROHM Semiconductors.
 * Author: Matti Vaittinen <matti.vaittien@fi.rohmeurope.com>
 */
#include <kunit/test.h>

#include <linux/linear_range.h>

/* First things first. I deeply dislike unit-tests. I have seen all the hell
 * breaking loose when people who think the unit tests are "the silver bullet"
 * to kill bugs get to decide how a company should implement testing strategy...
 *
 * Believe me, it may get _really_ ridiculous. It is tempting to think that
 * walking through all the possible execution branches will nail down 100% of
 * bugs. This may lead to ideas about demands to get certain % of "test
 * coverage" - measured as line coverage. And that is one of the worst things
 * you can do.
 *
 * Ask people to provide line coverage and they do. I've seen clever tools
 * which generate test cases to test the existing functions - and by default
 * these tools expect code to be correct and just generate checks which are
 * passing when ran against current code-base. Run this generator and you'll get
 * tests that do not test code is correct but just verify nothing changes.
 * Problem is that testing working code is pointless. And if it is not
 * working, your test must not assume it is working. You won't catch any bugs
 * by such tests. What you can do is to generate a huge amount of tests.
 * Especially if you were are asked to proivde 100% line-coverage x_x. So what
 * does these tests - which are not finding any bugs now - do?
 *
 * They add inertia to every future development. I think it was Terry Pratchet
 * who wrote someone having same impact as thick syrup has to chronometre.
 * Excessive amount of unit-tests have this effect to development. If you do
 * actually find _any_ bug from code in such environment and try fixing it...
 * ...chances are you also need to fix the test cases. In sunny day you fix one
 * test. But I've done refactoring which resulted 500+ broken tests (which had
 * really zero value other than proving to managers that we do do "quality")...
 *
 * After this being said - there are situations where UTs can be handy. If you
 * have algorithms which take some input and should produce output - then you
 * can implement few, carefully selected simple UT-cases which test this. I've
 * previously used this for example for netlink and device-tree data parsing
 * functions. Feed some data examples to functions and verify the output is as
 * expected. I am not covering all the cases but I will see the logic should be
 * working.
 *
 * Here we also do some minor testing. I don't want to go through all branches
 * or test more or less obvious things - but I want to see the main logic is
 * working. And I definitely don't want to add 500+ test cases that break when
 * some simple fix is done x_x. So - let's only add few, well selected tests
 * which ensure as much logic is good as possible.
 */

/*
 * Test Range 1:
 * selectors:	2	3	4	5	6
 * values (5):	10	20	30	40	50
 *
 * Test Range 2:
 * selectors:	7	8	9	10
 * values (4):	100	150	200	250
 */

#define RANGE1_MIN 10
#define RANGE1_MIN_SEL 2
#define RANGE1_STEP 10

/* 2, 3, 4, 5, 6 */
static const unsigned int range1_sels[] = { RANGE1_MIN_SEL, RANGE1_MIN_SEL + 1,
					    RANGE1_MIN_SEL + 2,
					    RANGE1_MIN_SEL + 3,
					    RANGE1_MIN_SEL + 4 };
/* 10, 20, 30, 40, 50 */
static const unsigned int range1_vals[] = { RANGE1_MIN, RANGE1_MIN +
					    RANGE1_STEP,
					    RANGE1_MIN + RANGE1_STEP * 2,
					    RANGE1_MIN + RANGE1_STEP * 3,
					    RANGE1_MIN + RANGE1_STEP * 4 };

#define RANGE2_MIN 100
#define RANGE2_MIN_SEL 7
#define RANGE2_STEP 50

/*  7, 8, 9, 10 */
static const unsigned int range2_sels[] = { RANGE2_MIN_SEL, RANGE2_MIN_SEL + 1,
					    RANGE2_MIN_SEL + 2,
					    RANGE2_MIN_SEL + 3 };
/* 100, 150, 200, 250 */
static const unsigned int range2_vals[] = { RANGE2_MIN, RANGE2_MIN +
					    RANGE2_STEP,
					    RANGE2_MIN + RANGE2_STEP * 2,
					    RANGE2_MIN + RANGE2_STEP * 3 };

#define RANGE1_NUM_VALS (ARRAY_SIZE(range1_vals))
#define RANGE2_NUM_VALS (ARRAY_SIZE(range2_vals))
#define RANGE_NUM_VALS (RANGE1_NUM_VALS + RANGE2_NUM_VALS)

#define RANGE1_MAX_SEL (RANGE1_MIN_SEL + RANGE1_NUM_VALS - 1)
#define RANGE1_MAX_VAL (range1_vals[RANGE1_NUM_VALS - 1])

#define RANGE2_MAX_SEL (RANGE2_MIN_SEL + RANGE2_NUM_VALS - 1)
#define RANGE2_MAX_VAL (range2_vals[RANGE2_NUM_VALS - 1])

#define SMALLEST_SEL RANGE1_MIN_SEL
#define SMALLEST_VAL RANGE1_MIN

static struct linear_range testr[] = {
	LINEAR_RANGE(RANGE1_MIN, RANGE1_MIN_SEL, RANGE1_MAX_SEL, RANGE1_STEP),
	LINEAR_RANGE(RANGE2_MIN, RANGE2_MIN_SEL, RANGE2_MAX_SEL, RANGE2_STEP),
};

static void range_test_get_value(struct kunit *test)
{
	int ret, i;
	unsigned int sel, val;

	for (i = 0; i < RANGE1_NUM_VALS; i++) {
		sel = range1_sels[i];
		ret = linear_range_get_value_array(&testr[0], 2, sel, &val);
		KUNIT_EXPECT_EQ(test, 0, ret);
		KUNIT_EXPECT_EQ(test, val, range1_vals[i]);
	}
	for (i = 0; i < RANGE2_NUM_VALS; i++) {
		sel = range2_sels[i];
		ret = linear_range_get_value_array(&testr[0], 2, sel, &val);
		KUNIT_EXPECT_EQ(test, 0, ret);
		KUNIT_EXPECT_EQ(test, val, range2_vals[i]);
	}
	ret = linear_range_get_value_array(&testr[0], 2, sel + 1, &val);
	KUNIT_EXPECT_NE(test, 0, ret);
}

static void range_test_get_selector_high(struct kunit *test)
{
	int ret, i;
	unsigned int sel;
	bool found;

	for (i = 0; i < RANGE1_NUM_VALS; i++) {
		ret = linear_range_get_selector_high(&testr[0], range1_vals[i],
						     &sel, &found);
		KUNIT_EXPECT_EQ(test, 0, ret);
		KUNIT_EXPECT_EQ(test, sel, range1_sels[i]);
		KUNIT_EXPECT_TRUE(test, found);
	}

	ret = linear_range_get_selector_high(&testr[0], RANGE1_MAX_VAL + 1,
					     &sel, &found);
	KUNIT_EXPECT_LE(test, ret, 0);

	ret = linear_range_get_selector_high(&testr[0], RANGE1_MIN - 1,
					     &sel, &found);
	KUNIT_EXPECT_EQ(test, 0, ret);
	KUNIT_EXPECT_FALSE(test, found);
	KUNIT_EXPECT_EQ(test, sel, range1_sels[0]);
}

static void range_test_get_value_amount(struct kunit *test)
{
	int ret;

	ret = linear_range_values_in_range_array(&testr[0], 2);
	KUNIT_EXPECT_EQ(test, (int)RANGE_NUM_VALS, ret);
}

static void range_test_get_selector_low(struct kunit *test)
{
	int i, ret;
	unsigned int sel;
	bool found;

	for (i = 0; i < RANGE1_NUM_VALS; i++) {
		ret = linear_range_get_selector_low_array(&testr[0], 2,
							  range1_vals[i], &sel,
							  &found);
		KUNIT_EXPECT_EQ(test, 0, ret);
		KUNIT_EXPECT_EQ(test, sel, range1_sels[i]);
		KUNIT_EXPECT_TRUE(test, found);
	}
	for (i = 0; i < RANGE2_NUM_VALS; i++) {
		ret = linear_range_get_selector_low_array(&testr[0], 2,
							  range2_vals[i], &sel,
							  &found);
		KUNIT_EXPECT_EQ(test, 0, ret);
		KUNIT_EXPECT_EQ(test, sel, range2_sels[i]);
		KUNIT_EXPECT_TRUE(test, found);
	}

	/*
	 * Seek value greater than range max => get_selector_*_low should
	 * return Ok - but set found to false as value is not in range
	 */
	ret = linear_range_get_selector_low_array(&testr[0], 2,
					range2_vals[RANGE2_NUM_VALS - 1] + 1,
					&sel, &found);

	KUNIT_EXPECT_EQ(test, 0, ret);
	KUNIT_EXPECT_EQ(test, sel, range2_sels[RANGE2_NUM_VALS - 1]);
	KUNIT_EXPECT_FALSE(test, found);
}

static struct kunit_case range_test_cases[] = {
	KUNIT_CASE(range_test_get_value_amount),
	KUNIT_CASE(range_test_get_selector_high),
	KUNIT_CASE(range_test_get_selector_low),
	KUNIT_CASE(range_test_get_value),
	{},
};

static struct kunit_suite range_test_module = {
	.name = "linear-ranges-test",
	.test_cases = range_test_cases,
};

kunit_test_suites(&range_test_module);

MODULE_DESCRIPTION("KUnit test for the linear_ranges helper");
MODULE_LICENSE("GPL");
