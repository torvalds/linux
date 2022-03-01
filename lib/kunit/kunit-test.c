// SPDX-License-Identifier: GPL-2.0
/*
 * KUnit test for core test infrastructure.
 *
 * Copyright (C) 2019, Google LLC.
 * Author: Brendan Higgins <brendanhiggins@google.com>
 */
#include <kunit/test.h>

#include "try-catch-impl.h"

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
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, ctx);
	test->priv = ctx;

	ctx->try_catch = kunit_kmalloc(test,
				       sizeof(*ctx->try_catch),
				       GFP_KERNEL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, ctx->try_catch);

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

/*
 * Context for testing test managed resources
 * is_resource_initialized is used to test arbitrary resources
 */
struct kunit_test_resource_context {
	struct kunit test;
	bool is_resource_initialized;
	int allocate_order[2];
	int free_order[2];
};

static int fake_resource_init(struct kunit_resource *res, void *context)
{
	struct kunit_test_resource_context *ctx = context;

	res->data = &ctx->is_resource_initialized;
	ctx->is_resource_initialized = true;
	return 0;
}

static void fake_resource_free(struct kunit_resource *res)
{
	bool *is_resource_initialized = res->data;

	*is_resource_initialized = false;
}

static void kunit_resource_test_init_resources(struct kunit *test)
{
	struct kunit_test_resource_context *ctx = test->priv;

	kunit_init_test(&ctx->test, "testing_test_init_test", NULL);

	KUNIT_EXPECT_TRUE(test, list_empty(&ctx->test.resources));
}

static void kunit_resource_test_alloc_resource(struct kunit *test)
{
	struct kunit_test_resource_context *ctx = test->priv;
	struct kunit_resource *res;
	kunit_resource_free_t free = fake_resource_free;

	res = kunit_alloc_and_get_resource(&ctx->test,
					   fake_resource_init,
					   fake_resource_free,
					   GFP_KERNEL,
					   ctx);

	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, res);
	KUNIT_EXPECT_PTR_EQ(test,
			    &ctx->is_resource_initialized,
			    (bool *)res->data);
	KUNIT_EXPECT_TRUE(test, list_is_last(&res->node, &ctx->test.resources));
	KUNIT_EXPECT_PTR_EQ(test, free, res->free);

	kunit_put_resource(res);
}

/*
 * Note: tests below use kunit_alloc_and_get_resource(), so as a consequence
 * they have a reference to the associated resource that they must release
 * via kunit_put_resource().  In normal operation, users will only
 * have to do this for cases where they use kunit_find_resource(), and the
 * kunit_alloc_resource() function will be used (which does not take a
 * resource reference).
 */
static void kunit_resource_test_destroy_resource(struct kunit *test)
{
	struct kunit_test_resource_context *ctx = test->priv;
	struct kunit_resource *res = kunit_alloc_and_get_resource(
			&ctx->test,
			fake_resource_init,
			fake_resource_free,
			GFP_KERNEL,
			ctx);

	kunit_put_resource(res);

	KUNIT_ASSERT_FALSE(test,
			   kunit_destroy_resource(&ctx->test,
						  kunit_resource_instance_match,
						  res->data));

	KUNIT_EXPECT_FALSE(test, ctx->is_resource_initialized);
	KUNIT_EXPECT_TRUE(test, list_empty(&ctx->test.resources));
}

static void kunit_resource_test_cleanup_resources(struct kunit *test)
{
	int i;
	struct kunit_test_resource_context *ctx = test->priv;
	struct kunit_resource *resources[5];

	for (i = 0; i < ARRAY_SIZE(resources); i++) {
		resources[i] = kunit_alloc_and_get_resource(&ctx->test,
							    fake_resource_init,
							    fake_resource_free,
							    GFP_KERNEL,
							    ctx);
		kunit_put_resource(resources[i]);
	}

	kunit_cleanup(&ctx->test);

	KUNIT_EXPECT_TRUE(test, list_empty(&ctx->test.resources));
}

static void kunit_resource_test_mark_order(int order_array[],
					   size_t order_size,
					   int key)
{
	int i;

	for (i = 0; i < order_size && order_array[i]; i++)
		;

	order_array[i] = key;
}

#define KUNIT_RESOURCE_TEST_MARK_ORDER(ctx, order_field, key)		       \
		kunit_resource_test_mark_order(ctx->order_field,	       \
					       ARRAY_SIZE(ctx->order_field),   \
					       key)

static int fake_resource_2_init(struct kunit_resource *res, void *context)
{
	struct kunit_test_resource_context *ctx = context;

	KUNIT_RESOURCE_TEST_MARK_ORDER(ctx, allocate_order, 2);

	res->data = ctx;

	return 0;
}

static void fake_resource_2_free(struct kunit_resource *res)
{
	struct kunit_test_resource_context *ctx = res->data;

	KUNIT_RESOURCE_TEST_MARK_ORDER(ctx, free_order, 2);
}

static int fake_resource_1_init(struct kunit_resource *res, void *context)
{
	struct kunit_test_resource_context *ctx = context;
	struct kunit_resource *res2;

	res2 = kunit_alloc_and_get_resource(&ctx->test,
					    fake_resource_2_init,
					    fake_resource_2_free,
					    GFP_KERNEL,
					    ctx);

	KUNIT_RESOURCE_TEST_MARK_ORDER(ctx, allocate_order, 1);

	res->data = ctx;

	kunit_put_resource(res2);

	return 0;
}

static void fake_resource_1_free(struct kunit_resource *res)
{
	struct kunit_test_resource_context *ctx = res->data;

	KUNIT_RESOURCE_TEST_MARK_ORDER(ctx, free_order, 1);
}

/*
 * TODO(brendanhiggins@google.com): replace the arrays that keep track of the
 * order of allocation and freeing with strict mocks using the IN_SEQUENCE macro
 * to assert allocation and freeing order when the feature becomes available.
 */
static void kunit_resource_test_proper_free_ordering(struct kunit *test)
{
	struct kunit_test_resource_context *ctx = test->priv;
	struct kunit_resource *res;

	/* fake_resource_1 allocates a fake_resource_2 in its init. */
	res = kunit_alloc_and_get_resource(&ctx->test,
					   fake_resource_1_init,
					   fake_resource_1_free,
					   GFP_KERNEL,
					   ctx);

	/*
	 * Since fake_resource_2_init calls KUNIT_RESOURCE_TEST_MARK_ORDER
	 * before returning to fake_resource_1_init, it should be the first to
	 * put its key in the allocate_order array.
	 */
	KUNIT_EXPECT_EQ(test, ctx->allocate_order[0], 2);
	KUNIT_EXPECT_EQ(test, ctx->allocate_order[1], 1);

	kunit_put_resource(res);

	kunit_cleanup(&ctx->test);

	/*
	 * Because fake_resource_2 finishes allocation before fake_resource_1,
	 * fake_resource_1 should be freed first since it could depend on
	 * fake_resource_2.
	 */
	KUNIT_EXPECT_EQ(test, ctx->free_order[0], 1);
	KUNIT_EXPECT_EQ(test, ctx->free_order[1], 2);
}

static void kunit_resource_test_static(struct kunit *test)
{
	struct kunit_test_resource_context ctx;
	struct kunit_resource res;

	KUNIT_EXPECT_EQ(test, kunit_add_resource(test, NULL, NULL, &res, &ctx),
			0);

	KUNIT_EXPECT_PTR_EQ(test, res.data, (void *)&ctx);

	kunit_cleanup(test);

	KUNIT_EXPECT_TRUE(test, list_empty(&test->resources));
}

static void kunit_resource_test_named(struct kunit *test)
{
	struct kunit_resource res1, res2, *found = NULL;
	struct kunit_test_resource_context ctx;

	KUNIT_EXPECT_EQ(test,
			kunit_add_named_resource(test, NULL, NULL, &res1,
						 "resource_1", &ctx),
			0);
	KUNIT_EXPECT_PTR_EQ(test, res1.data, (void *)&ctx);

	KUNIT_EXPECT_EQ(test,
			kunit_add_named_resource(test, NULL, NULL, &res1,
						 "resource_1", &ctx),
			-EEXIST);

	KUNIT_EXPECT_EQ(test,
			kunit_add_named_resource(test, NULL, NULL, &res2,
						 "resource_2", &ctx),
			0);

	found = kunit_find_named_resource(test, "resource_1");

	KUNIT_EXPECT_PTR_EQ(test, found, &res1);

	if (found)
		kunit_put_resource(&res1);

	KUNIT_EXPECT_EQ(test, kunit_destroy_named_resource(test, "resource_2"),
			0);

	kunit_cleanup(test);

	KUNIT_EXPECT_TRUE(test, list_empty(&test->resources));
}

static int kunit_resource_test_init(struct kunit *test)
{
	struct kunit_test_resource_context *ctx =
			kzalloc(sizeof(*ctx), GFP_KERNEL);

	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, ctx);

	test->priv = ctx;

	kunit_init_test(&ctx->test, "test_test_context", NULL);

	return 0;
}

static void kunit_resource_test_exit(struct kunit *test)
{
	struct kunit_test_resource_context *ctx = test->priv;

	kunit_cleanup(&ctx->test);
	kfree(ctx);
}

static struct kunit_case kunit_resource_test_cases[] = {
	KUNIT_CASE(kunit_resource_test_init_resources),
	KUNIT_CASE(kunit_resource_test_alloc_resource),
	KUNIT_CASE(kunit_resource_test_destroy_resource),
	KUNIT_CASE(kunit_resource_test_cleanup_resources),
	KUNIT_CASE(kunit_resource_test_proper_free_ordering),
	KUNIT_CASE(kunit_resource_test_static),
	KUNIT_CASE(kunit_resource_test_named),
	{}
};

static struct kunit_suite kunit_resource_test_suite = {
	.name = "kunit-resource-test",
	.init = kunit_resource_test_init,
	.exit = kunit_resource_test_exit,
	.test_cases = kunit_resource_test_cases,
};

static void kunit_log_test(struct kunit *test);

static struct kunit_case kunit_log_test_cases[] = {
	KUNIT_CASE(kunit_log_test),
	{}
};

static struct kunit_suite kunit_log_test_suite = {
	.name = "kunit-log-test",
	.test_cases = kunit_log_test_cases,
};

static void kunit_log_test(struct kunit *test)
{
	struct kunit_suite suite;

	suite.log = kunit_kzalloc(test, KUNIT_LOG_SIZE, GFP_KERNEL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, suite.log);

	kunit_log(KERN_INFO, test, "put this in log.");
	kunit_log(KERN_INFO, test, "this too.");
	kunit_log(KERN_INFO, &suite, "add to suite log.");
	kunit_log(KERN_INFO, &suite, "along with this.");

#ifdef CONFIG_KUNIT_DEBUGFS
	KUNIT_EXPECT_NOT_ERR_OR_NULL(test,
				     strstr(test->log, "put this in log."));
	KUNIT_EXPECT_NOT_ERR_OR_NULL(test,
				     strstr(test->log, "this too."));
	KUNIT_EXPECT_NOT_ERR_OR_NULL(test,
				     strstr(suite.log, "add to suite log."));
	KUNIT_EXPECT_NOT_ERR_OR_NULL(test,
				     strstr(suite.log, "along with this."));
#else
	KUNIT_EXPECT_PTR_EQ(test, test->log, (char *)NULL);
#endif
}

static void kunit_status_set_failure_test(struct kunit *test)
{
	struct kunit fake;

	kunit_init_test(&fake, "fake test", NULL);

	KUNIT_EXPECT_EQ(test, fake.status, (enum kunit_status)KUNIT_SUCCESS);
	kunit_set_failure(&fake);
	KUNIT_EXPECT_EQ(test, fake.status, (enum kunit_status)KUNIT_FAILURE);
}

static void kunit_status_mark_skipped_test(struct kunit *test)
{
	struct kunit fake;

	kunit_init_test(&fake, "fake test", NULL);

	/* Before: Should be SUCCESS with no comment. */
	KUNIT_EXPECT_EQ(test, fake.status, KUNIT_SUCCESS);
	KUNIT_EXPECT_STREQ(test, fake.status_comment, "");

	/* Mark the test as skipped. */
	kunit_mark_skipped(&fake, "Accepts format string: %s", "YES");

	/* After: Should be SKIPPED with our comment. */
	KUNIT_EXPECT_EQ(test, fake.status, (enum kunit_status)KUNIT_SKIPPED);
	KUNIT_EXPECT_STREQ(test, fake.status_comment, "Accepts format string: YES");
}

static struct kunit_case kunit_status_test_cases[] = {
	KUNIT_CASE(kunit_status_set_failure_test),
	KUNIT_CASE(kunit_status_mark_skipped_test),
	{}
};

static struct kunit_suite kunit_status_test_suite = {
	.name = "kunit_status",
	.test_cases = kunit_status_test_cases,
};

kunit_test_suites(&kunit_try_catch_test_suite, &kunit_resource_test_suite,
		  &kunit_log_test_suite, &kunit_status_test_suite);

MODULE_LICENSE("GPL v2");
