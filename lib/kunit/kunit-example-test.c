// SPDX-License-Identifier: GPL-2.0
/*
 * Example KUnit test to show how to use KUnit.
 *
 * Copyright (C) 2019, Google LLC.
 * Author: Brendan Higgins <brendanhiggins@google.com>
 */

#include <kunit/test.h>
#include <kunit/static_stub.h>

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
 * This is run once after each test case, see the comment on
 * example_test_suite for more information.
 */
static void example_test_exit(struct kunit *test)
{
	kunit_info(test, "cleaning up\n");
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
 * This is run once after all test cases in the suite.
 * See the comment on example_test_suite for more information.
 */
static void example_test_exit_suite(struct kunit_suite *suite)
{
	kunit_info(suite, "exiting suite\n");
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
	const u32 array1[] = { 0x0F, 0xFF };
	const u32 array2[] = { 0x1F, 0xFF };

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

	/* Memory block assertions */
	KUNIT_EXPECT_MEMEQ(test, array1, array1, sizeof(array1));
	KUNIT_EXPECT_MEMNEQ(test, array1, array2, sizeof(array1));

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

/* This is a function we'll replace with static stubs. */
static int add_one(int i)
{
	/* This will trigger the stub if active. */
	KUNIT_STATIC_STUB_REDIRECT(add_one, i);

	return i + 1;
}

/* This is used as a replacement for the above function. */
static int subtract_one(int i)
{
	/* We don't need to trigger the stub from the replacement. */

	return i - 1;
}

/*
 * This test shows the use of static stubs.
 */
static void example_static_stub_test(struct kunit *test)
{
	/* By default, function is not stubbed. */
	KUNIT_EXPECT_EQ(test, add_one(1), 2);

	/* Replace add_one() with subtract_one(). */
	kunit_activate_static_stub(test, add_one, subtract_one);

	/* add_one() is now replaced. */
	KUNIT_EXPECT_EQ(test, add_one(1), 0);

	/* Return add_one() to normal. */
	kunit_deactivate_static_stub(test, add_one);
	KUNIT_EXPECT_EQ(test, add_one(1), 2);
}

static const struct example_param {
	int value;
} example_params_array[] = {
	{ .value = 3, },
	{ .value = 2, },
	{ .value = 1, },
	{ .value = 0, },
};

static void example_param_get_desc(const struct example_param *p, char *desc)
{
	snprintf(desc, KUNIT_PARAM_DESC_SIZE, "example value %d", p->value);
}

KUNIT_ARRAY_PARAM(example, example_params_array, example_param_get_desc);

/*
 * This test shows the use of params.
 */
static void example_params_test(struct kunit *test)
{
	const struct example_param *param = test->param_value;

	/* By design, param pointer will not be NULL */
	KUNIT_ASSERT_NOT_NULL(test, param);

	/* Test can be skipped on unsupported param values */
	if (!is_power_of_2(param->value))
		kunit_skip(test, "unsupported param value %d", param->value);

	/* You can use param values for parameterized testing */
	KUNIT_EXPECT_EQ(test, param->value % param->value, 0);
}

/*
 * This test shows the use of test->priv.
 */
static void example_priv_test(struct kunit *test)
{
	/* unless setup in suite->init(), test->priv is NULL */
	KUNIT_ASSERT_NULL(test, test->priv);

	/* but can be used to pass arbitrary data to other functions */
	test->priv = kunit_kzalloc(test, 1, GFP_KERNEL);
	KUNIT_EXPECT_NOT_NULL(test, test->priv);
	KUNIT_ASSERT_PTR_EQ(test, test->priv, kunit_get_current_test()->priv);
}

/*
 * This test should always pass. Can be used to practice filtering attributes.
 */
static void example_slow_test(struct kunit *test)
{
	KUNIT_EXPECT_EQ(test, 1 + 1, 2);
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
	KUNIT_CASE(example_static_stub_test),
	KUNIT_CASE(example_priv_test),
	KUNIT_CASE_PARAM(example_params_test, example_gen_params),
	KUNIT_CASE_SLOW(example_slow_test),
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
	.exit = example_test_exit,
	.suite_init = example_test_init_suite,
	.suite_exit = example_test_exit_suite,
	.test_cases = example_test_cases,
};

/*
 * This registers the above test suite telling KUnit that this is a suite of
 * tests that need to be run.
 */
kunit_test_suites(&example_test_suite);

static int __init init_add(int x, int y)
{
	return (x + y);
}

/*
 * This test should always pass. Can be used to test init suites.
 */
static void __init example_init_test(struct kunit *test)
{
	KUNIT_EXPECT_EQ(test, init_add(1, 1), 2);
}

/*
 * The kunit_case struct cannot be marked as __initdata as this will be
 * used in debugfs to retrieve results after test has run
 */
static struct kunit_case __refdata example_init_test_cases[] = {
	KUNIT_CASE(example_init_test),
	{}
};

/*
 * The kunit_suite struct cannot be marked as __initdata as this will be
 * used in debugfs to retrieve results after test has run
 */
static struct kunit_suite example_init_test_suite = {
	.name = "example_init",
	.test_cases = example_init_test_cases,
};

/*
 * This registers the test suite and marks the suite as using init data
 * and/or functions.
 */
kunit_test_init_section_suites(&example_init_test_suite);

MODULE_LICENSE("GPL v2");
