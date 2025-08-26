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
 * If the function to be replaced is static within a module it is
 * useful to export a pointer to that function instead of having
 * to change the static function to a non-static exported function.
 *
 * This pointer simulates a module exporting a pointer to a static
 * function.
 */
static int (* const add_one_fn_ptr)(int i) = add_one;

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

/*
 * This test shows the use of static stubs when the function being
 * replaced is provided as a pointer-to-function instead of the
 * actual function. This is useful for providing access to static
 * functions in a module by exporting a pointer to that function
 * instead of having to change the static function to a non-static
 * exported function.
 */
static void example_static_stub_using_fn_ptr_test(struct kunit *test)
{
	/* By default, function is not stubbed. */
	KUNIT_EXPECT_EQ(test, add_one(1), 2);

	/* Replace add_one() with subtract_one(). */
	kunit_activate_static_stub(test, add_one_fn_ptr, subtract_one);

	/* add_one() is now replaced. */
	KUNIT_EXPECT_EQ(test, add_one(1), 0);

	/* Return add_one() to normal. */
	kunit_deactivate_static_stub(test, add_one_fn_ptr);
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
 * This custom function allocates memory and sets the information we want
 * stored in the kunit_resource->data field.
 */
static int example_resource_init(struct kunit_resource *res, void *context)
{
	int *info = kmalloc(sizeof(*info), GFP_KERNEL);

	if (!info)
		return -ENOMEM;
	*info = *(int *)context;
	res->data = info;
	return 0;
}

/*
 * This function deallocates memory for the kunit_resource->data field.
 */
static void example_resource_free(struct kunit_resource *res)
{
	kfree(res->data);
}

/*
 * This match function is invoked by kunit_find_resource() to locate
 * a test resource based on certain criteria.
 */
static bool example_resource_alloc_match(struct kunit *test,
					 struct kunit_resource *res,
					 void *match_data)
{
	return res->data && res->free == example_resource_free;
}

/*
 * This is an example of a function that provides a description for each of the
 * parameters in a parameterized test.
 */
static void example_param_array_get_desc(struct kunit *test, const void *p, char *desc)
{
	const struct example_param *param = p;

	snprintf(desc, KUNIT_PARAM_DESC_SIZE,
		 "example check if %d is less than or equal to 3", param->value);
}

/*
 * This function gets passed in the parameterized test context i.e. the
 * struct kunit belonging to the parameterized test. You can use this function
 * to add resources you want shared across the whole parameterized test or
 * for additional setup.
 */
static int example_param_init(struct kunit *test)
{
	int ctx = 3; /* Data to be stored. */
	size_t arr_size = ARRAY_SIZE(example_params_array);

	/*
	 * This allocates a struct kunit_resource, sets its data field to
	 * ctx, and adds it to the struct kunit's resources list. Note that
	 * this is parameterized test managed. So, it doesn't need to have
	 * a custom exit function to deallocation as it will get cleaned up at
	 * the end of the parameterized test.
	 */
	void *data = kunit_alloc_resource(test, example_resource_init, example_resource_free,
					  GFP_KERNEL, &ctx);

	if (!data)
		return -ENOMEM;
	/*
	 * Pass the parameter array information to the parameterized test context
	 * struct kunit. Note that you will need to provide kunit_array_gen_params()
	 * as the generator function to KUNIT_CASE_PARAM_WITH_INIT() when registering
	 * a parameter array this route.
	 */
	kunit_register_params_array(test, example_params_array, arr_size,
				    example_param_array_get_desc);
	return 0;
}

/*
 * This is an example of a test that uses shared resources available in the
 * parameterized test context.
 */
static void example_params_test_with_init(struct kunit *test)
{
	int threshold;
	struct kunit_resource *res;
	const struct example_param *param = test->param_value;

	/* By design, param pointer will not be NULL. */
	KUNIT_ASSERT_NOT_NULL(test, param);

	/*
	 * Here we pass test->parent to search for shared resources in the
	 * parameterized test context.
	 */
	res = kunit_find_resource(test->parent, example_resource_alloc_match, NULL);

	KUNIT_ASSERT_NOT_NULL(test, res);

	/* Since kunit_resource->data is a void pointer we need to typecast it. */
	threshold = *((int *)res->data);

	/* Assert that the parameter is less than or equal to a certain threshold. */
	KUNIT_ASSERT_LE(test, param->value, threshold);

	/* This decreases the reference count after calling kunit_find_resource(). */
	kunit_put_resource(res);
}

/*
 * Helper function to create a parameter array of Fibonacci numbers. This example
 * highlights a parameter generation scenario that is:
 * 1. Not feasible to fully pre-generate at compile time.
 * 2. Challenging to implement with a standard generate_params() function,
 * as it only provides the previous parameter, while Fibonacci requires
 * access to two preceding values for calculation.
 */
static void *make_fibonacci_params(struct kunit *test, size_t seq_size)
{
	int *seq;

	if (seq_size <= 0)
		return NULL;
	/*
	 * Using kunit_kmalloc_array here ties the lifetime of the array to
	 * the parameterized test i.e. it will get automatically cleaned up
	 * by KUnit after the parameterized test finishes.
	 */
	seq = kunit_kmalloc_array(test, seq_size, sizeof(int), GFP_KERNEL);

	if (!seq)
		return NULL;
	if (seq_size >= 1)
		seq[0] = 0;
	if (seq_size >= 2)
		seq[1] = 1;
	for (int i = 2; i < seq_size; i++)
		seq[i] = seq[i - 1] + seq[i - 2];
	return seq;
}

/*
 * This is an example of a function that provides a description for each of the
 * parameters.
 */
static void example_param_dynamic_arr_get_desc(struct kunit *test, const void *p, char *desc)
{
	const int *fib_num = p;

	snprintf(desc, KUNIT_PARAM_DESC_SIZE, "fibonacci param: %d", *fib_num);
}

/*
 * Example of a parameterized test param_init() function that registers a dynamic
 * array of parameters.
 */
static int example_param_init_dynamic_arr(struct kunit *test)
{
	size_t seq_size;
	int *fibonacci_params;

	kunit_info(test, "initializing parameterized test\n");

	seq_size = 6;
	fibonacci_params = make_fibonacci_params(test, seq_size);

	if (!fibonacci_params)
		return -ENOMEM;

	/*
	 * Passes the dynamic parameter array information to the parameterized test
	 * context struct kunit. The array and its metadata will be stored in
	 * test->parent->params_array. The array itself will be located in
	 * params_data.params.
	 *
	 * Note that you will need to pass kunit_array_gen_params() as the
	 * generator function to KUNIT_CASE_PARAM_WITH_INIT() when registering
	 * a parameter array this route.
	 */
	kunit_register_params_array(test, fibonacci_params, seq_size,
				    example_param_dynamic_arr_get_desc);
	return 0;
}

/*
 * Example of a parameterized test param_exit() function that outputs a log
 * at the end of the parameterized test. It could also be used for any other
 * teardown logic.
 */
static void example_param_exit_dynamic_arr(struct kunit *test)
{
	kunit_info(test, "exiting parameterized test\n");
}

/*
 * Example of test that uses the registered dynamic array to perform assertions
 * and expectations.
 */
static void example_params_test_with_init_dynamic_arr(struct kunit *test)
{
	const int *param = test->param_value;
	int param_val;

	/* By design, param pointer will not be NULL. */
	KUNIT_ASSERT_NOT_NULL(test, param);

	param_val = *param;
	KUNIT_EXPECT_EQ(test, param_val - param_val, 0);
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
	KUNIT_CASE(example_static_stub_using_fn_ptr_test),
	KUNIT_CASE(example_priv_test),
	KUNIT_CASE_PARAM(example_params_test, example_gen_params),
	KUNIT_CASE_PARAM_WITH_INIT(example_params_test_with_init, kunit_array_gen_params,
				   example_param_init, NULL),
	KUNIT_CASE_PARAM_WITH_INIT(example_params_test_with_init_dynamic_arr,
				   kunit_array_gen_params, example_param_init_dynamic_arr,
				   example_param_exit_dynamic_arr),
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

MODULE_DESCRIPTION("Example KUnit test suite");
MODULE_LICENSE("GPL v2");
