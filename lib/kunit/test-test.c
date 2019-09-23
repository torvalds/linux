// SPDX-License-Identifier: GPL-2.0
/*
 * KUnit test for core test infrastructure.
 *
 * Copyright (C) 2019, Google LLC.
 * Author: Brendan Higgins <brendanhiggins@google.com>
 */
#include <kunit/test.h>

struct kunit_try_catch_test_context {
	struct kunit_try_catch *try_catch;
	bool function_called;
};

static void kunit_test_successful_try(void *data)
{
	struct kunit *test = data;
	struct kunit_try_catch_test_context *ctx = test->priv;

	ctx->function_called = true;
}

static void kunit_test_no_catch(void *data)
{
	struct kunit *test = data;

	KUNIT_FAIL(test, "Catch should not be called\n");
}

static void kunit_test_try_catch_successful_try_no_catch(struct kunit *test)
{
	struct kunit_try_catch_test_context *ctx = test->priv;
	struct kunit_try_catch *try_catch = ctx->try_catch;

	kunit_try_catch_init(try_catch,
			     test,
			     kunit_test_successful_try,
			     kunit_test_no_catch);
	kunit_try_catch_run(try_catch, test);

	KUNIT_EXPECT_TRUE(test, ctx->function_called);
}

static void kunit_test_unsuccessful_try(void *data)
{
	struct kunit *test = data;
	struct kunit_try_catch_test_context *ctx = test->priv;
	struct kunit_try_catch *try_catch = ctx->try_catch;

	kunit_try_catch_throw(try_catch);
	KUNIT_FAIL(test, "This line should never be reached\n");
}

static void kunit_test_catch(void *data)
{
	struct kunit *test = data;
	struct kunit_try_catch_test_context *ctx = test->priv;

	ctx->function_called = true;
}

static void kunit_test_try_catch_unsuccessful_try_does_catch(struct kunit *test)
{
	struct kunit_try_catch_test_context *ctx = test->priv;
	struct kunit_try_catch *try_catch = ctx->try_catch;

	kunit_try_catch_init(try_catch,
			     test,
			     kunit_test_unsuccessful_try,
			     kunit_test_catch);
	kunit_try_catch_run(try_catch, test);

	KUNIT_EXPECT_TRUE(test, ctx->function_called);
}

static int kunit_try_catch_test_init(struct kunit *test)
{
	struct kunit_try_catch_test_context *ctx;

	ctx = kunit_kzalloc(test, sizeof(*ctx), GFP_KERNEL);
	if (!ctx)
		return -ENOMEM;

	test->priv = ctx;

	ctx->try_catch = kunit_kmalloc(test,
				       sizeof(*ctx->try_catch),
				       GFP_KERNEL);
	if (!ctx->try_catch)
		return -ENOMEM;

	return 0;
}

static struct kunit_case kunit_try_catch_test_cases[] = {
	KUNIT_CASE(kunit_test_try_catch_successful_try_no_catch),
	KUNIT_CASE(kunit_test_try_catch_unsuccessful_try_does_catch),
	{}
};

static struct kunit_suite kunit_try_catch_test_suite = {
	.name = "kunit-try-catch-test",
	.init = kunit_try_catch_test_init,
	.test_cases = kunit_try_catch_test_cases,
};
kunit_test_suite(kunit_try_catch_test_suite);
