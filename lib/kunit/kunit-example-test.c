// SPDX-License-Identifier: GPL-2.0
/*
 * Example KUnit test to show how to use KUnit.
 *
 * Copyright (C) 2019, Google LLC.
 * Author: Brendan Higgins <brendanhiggins@google.com>
 */

#include <kunit/test.h>

/*
 * This is the most fundamental element of KUnit, the test case. A test case
 * makes a set EXPECTATIONs and ASSERTIONs about the behavior of some code; if
 * any expectations or assertions are not met, the test fails; otherwise, the
 * test passes.
 *
 * In KUnit, a test case is just a function with the signature
 * `void (*)(struct kunit *)`. `struct kunit` is a context object that stores
 * information about the current test.
 */
static void example_simple_test(struct kunit *test)
{
	/*
	 * This is an EXPECTATION; it is how KUnit tests things. When you want
	 * to test a piece of code, you set some expectations about what the
	 * code should do. KUnit then runs the test and verifies that the code's
	 * behavior matched what was expected.
	 */
	KUNIT_EXPECT_EQ(test, 1 + 1, 2);
}

/*
 * This is run once before each test case, see the comment on
 * example_test_suite for more information.
 */
static int example_test_init(struct kunit *test)
{
	kunit_info(test, "initializing\n");

	return 0;
}

/*
 * This is run once before all test cases in the suite.
 * See the comment on example_test_suite for more information.
 */
static int example_test_init_suite(struct kunit_suite *suite)
{
	kunit_info(suite, "initializing suite\n");

	return 0;
}

/*
 * This test should always be skipped.
 */
static void example_skip_test(struct kunit *test)
{
	/* This line should run */
	kunit_info(test, "You should not see a line below.");

	/* Skip (and abort) the test */
	kunit_skip(test, "this test should be skipped");

	/* This line should not execute */
	KUNIT_FAIL(test, "You should not see this line.");
}

/*
 * This test should always be marked skipped.
 */
static void example_mark_skipped_test(struct kunit *test)
{
	/* This line should run */
	kunit_info(test, "You should see a line below.");

	/* Skip (but do not abort) the test */
	kunit_mark_skipped(test, "this test should be skipped");

	/* This line should run */
	kunit_info(test, "You should see this line.");
}

/*
 * This test shows off all the types of KUNIT_EXPECT macros.
 */
static void example_all_expect_macros_test(struct kunit *test)
{
	/* Boolean assertions */
	KUNIT_EXPECT_TRUE(test, true);
	KUNIT_EXPECT_FALSE(test, false);

	/* Integer assertions */
	KUNIT_EXPECT_EQ(test, 1, 1); /* check == */
	KUNIT_EXPECT_GE(test, 1, 1); /* check >= */
	KUNIT_EXPECT_LE(test, 1, 1); /* check <= */
	KUNIT_EXPECT_NE(test, 1, 0); /* check != */
	KUNIT_EXPECT_GT(test, 1, 0); /* check >  */
	KUNIT_EXPECT_LT(test, 0, 1); /* check <  */

	/* Pointer assertions */
	KUNIT_EXPECT_NOT_ERR_OR_NULL(test, test);
	KUNIT_EXPECT_PTR_EQ(test, NULL, NULL);
	KUNIT_EXPECT_PTR_NE(test, test, NULL);
	KUNIT_EXPECT_NULL(test, NULL);
	KUNIT_EXPECT_NOT_NULL(test, test);

	/* String assertions */
	KUNIT_EXPECT_STREQ(test, "hi", "hi");
	KUNIT_EXPECT_STRNEQ(test, "hi", "bye");

	/*
	 * There are also ASSERT variants of all of the above that abort test
	 * execution if they fail. Useful for memory allocations, etc.
	 */
	KUNIT_ASSERT_GT(test, sizeof(char), 0);

	/*
	 * There are also _MSG variants of all of the above that let you include
	 * additional text on failure.
	 */
	KUNIT_EXPECT_GT_MSG(test, sizeof(int), 0, "Your ints are 0-bit?!");
	KUNIT_ASSERT_GT_MSG(test, sizeof(int), 0, "Your ints are 0-bit?!");
}

/*
 * Here we make a list of all the test cases we want to add to the test suite
 * below.
 */
static struct kunit_case example_test_cases[] = {
	/*
	 * This is a helper to create a test case object from a test case
	 * function; its exact function is not important to understand how to
	 * use KUnit, just know that this is how you associate test cases with a
	 * test suite.
	 */
	KUNIT_CASE(example_simple_test),
	KUNIT_CASE(example_skip_test),
	KUNIT_CASE(example_mark_skipped_test),
	KUNIT_CASE(example_all_expect_macros_test),
	{}
};

/*
 * This defines a suite or grouping of tests.
 *
 * Test cases are defined as belonging to the suite by adding them to
 * `kunit_cases`.
 *
 * Often it is desirable to run some function which will set up things which
 * will be used by every test; this is accomplished with an `init` function
 * which runs before each test case is invoked. Similarly, an `exit` function
 * may be specified which runs after every test case and can be used to for
 * cleanup. For clarity, running tests in a test suite would behave as follows:
 *
 * suite.suite_init(suite);
 * suite.init(test);
 * suite.test_case[0](test);
 * suite.exit(test);
 * suite.init(test);
 * suite.test_case[1](test);
 * suite.exit(test);
 * suite.suite_exit(suite);
 * ...;
 */
static struct kunit_suite example_test_suite = {
	.name = "example",
	.init = example_test_init,
	.suite_init = example_test_init_suite,
	.test_cases = example_test_cases,
};

/*
 * This registers the above test suite telling KUnit that this is a suite of
 * tests that need to be run.
 */
kunit_test_suites(&example_test_suite);

MODULE_LICENSE("GPL v2");
