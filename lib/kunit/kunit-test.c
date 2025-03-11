// SPDX-License-Identifier: GPL-2.0
/*
 * KUnit test for core test infrastructure.
 *
 * Copyright (C) 2019, Google LLC.
 * Author: Brendan Higgins <brendanhiggins@google.com>
 */
#include "linux/gfp_types.h"
#include <kunit/test.h>
#include <kunit/test-bug.h>

#include <linux/device.h>
#include <kunit/device.h>

#include "string-stream.h"
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

#if IS_ENABLED(CONFIG_KUNIT_FAULT_TEST)

static void kunit_test_null_dereference(void *data)
{
	struct kunit *test = data;
	int *null = NULL;

	*null = 0;

	KUNIT_FAIL(test, "This line should never be reached\n");
}

static void kunit_test_fault_null_dereference(struct kunit *test)
{
	struct kunit_try_catch_test_context *ctx = test->priv;
	struct kunit_try_catch *try_catch = ctx->try_catch;

	kunit_try_catch_init(try_catch,
			     test,
			     kunit_test_null_dereference,
			     kunit_test_catch);
	kunit_try_catch_run(try_catch, test);

	KUNIT_EXPECT_EQ(test, try_catch->try_result, -EINTR);
	KUNIT_EXPECT_TRUE(test, ctx->function_called);
}

#endif /* CONFIG_KUNIT_FAULT_TEST */

static struct kunit_case kunit_fault_test_cases[] = {
#if IS_ENABLED(CONFIG_KUNIT_FAULT_TEST)
	KUNIT_CASE(kunit_test_fault_null_dereference),
#endif /* CONFIG_KUNIT_FAULT_TEST */
	{}
};

static struct kunit_suite kunit_fault_test_suite = {
	.name = "kunit_fault",
	.init = kunit_try_catch_test_init,
	.test_cases = kunit_fault_test_cases,
};

/*
 * Context for testing test managed resources
 * is_resource_initialized is used to test arbitrary resources
 */
struct kunit_test_resource_context {
	struct kunit test;
	bool is_resource_initialized;
	int allocate_order[2];
	int free_order[4];
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

static inline bool kunit_resource_instance_match(struct kunit *test,
						 struct kunit_resource *res,
						 void *match_data)
{
	return res->data == match_data;
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

static void kunit_resource_test_remove_resource(struct kunit *test)
{
	struct kunit_test_resource_context *ctx = test->priv;
	struct kunit_resource *res = kunit_alloc_and_get_resource(
			&ctx->test,
			fake_resource_init,
			fake_resource_free,
			GFP_KERNEL,
			ctx);

	/* The resource is in the list */
	KUNIT_EXPECT_FALSE(test, list_empty(&ctx->test.resources));

	/* Remove the resource. The pointer is still valid, but it can't be
	 * found.
	 */
	kunit_remove_resource(test, res);
	KUNIT_EXPECT_TRUE(test, list_empty(&ctx->test.resources));
	/* We haven't been freed yet. */
	KUNIT_EXPECT_TRUE(test, ctx->is_resource_initialized);

	/* Removing the resource multiple times is valid. */
	kunit_remove_resource(test, res);
	KUNIT_EXPECT_TRUE(test, list_empty(&ctx->test.resources));
	/* Despite having been removed twice (from only one reference), the
	 * resource still has not been freed.
	 */
	KUNIT_EXPECT_TRUE(test, ctx->is_resource_initialized);

	/* Free the resource. */
	kunit_put_resource(res);
	KUNIT_EXPECT_FALSE(test, ctx->is_resource_initialized);
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

static void increment_int(void *ctx)
{
	int *i = (int *)ctx;
	(*i)++;
}

static void kunit_resource_test_action(struct kunit *test)
{
	int num_actions = 0;

	kunit_add_action(test, increment_int, &num_actions);
	KUNIT_EXPECT_EQ(test, num_actions, 0);
	kunit_cleanup(test);
	KUNIT_EXPECT_EQ(test, num_actions, 1);

	/* Once we've cleaned up, the action queue is empty. */
	kunit_cleanup(test);
	KUNIT_EXPECT_EQ(test, num_actions, 1);

	/* Check the same function can be deferred multiple times. */
	kunit_add_action(test, increment_int, &num_actions);
	kunit_add_action(test, increment_int, &num_actions);
	kunit_cleanup(test);
	KUNIT_EXPECT_EQ(test, num_actions, 3);
}
static void kunit_resource_test_remove_action(struct kunit *test)
{
	int num_actions = 0;

	kunit_add_action(test, increment_int, &num_actions);
	KUNIT_EXPECT_EQ(test, num_actions, 0);

	kunit_remove_action(test, increment_int, &num_actions);
	kunit_cleanup(test);
	KUNIT_EXPECT_EQ(test, num_actions, 0);
}
static void kunit_resource_test_release_action(struct kunit *test)
{
	int num_actions = 0;

	kunit_add_action(test, increment_int, &num_actions);
	KUNIT_EXPECT_EQ(test, num_actions, 0);
	/* Runs immediately on trigger. */
	kunit_release_action(test, increment_int, &num_actions);
	KUNIT_EXPECT_EQ(test, num_actions, 1);

	/* Doesn't run again on test exit. */
	kunit_cleanup(test);
	KUNIT_EXPECT_EQ(test, num_actions, 1);
}
static void action_order_1(void *ctx)
{
	struct kunit_test_resource_context *res_ctx = (struct kunit_test_resource_context *)ctx;

	KUNIT_RESOURCE_TEST_MARK_ORDER(res_ctx, free_order, 1);
	kunit_log(KERN_INFO, current->kunit_test, "action_order_1");
}
static void action_order_2(void *ctx)
{
	struct kunit_test_resource_context *res_ctx = (struct kunit_test_resource_context *)ctx;

	KUNIT_RESOURCE_TEST_MARK_ORDER(res_ctx, free_order, 2);
	kunit_log(KERN_INFO, current->kunit_test, "action_order_2");
}
static void kunit_resource_test_action_ordering(struct kunit *test)
{
	struct kunit_test_resource_context *ctx = test->priv;

	kunit_add_action(test, action_order_1, ctx);
	kunit_add_action(test, action_order_2, ctx);
	kunit_add_action(test, action_order_1, ctx);
	kunit_add_action(test, action_order_2, ctx);
	kunit_remove_action(test, action_order_1, ctx);
	kunit_release_action(test, action_order_2, ctx);
	kunit_cleanup(test);

	/* [2 is triggered] [2], [(1 is cancelled)] [1] */
	KUNIT_EXPECT_EQ(test, ctx->free_order[0], 2);
	KUNIT_EXPECT_EQ(test, ctx->free_order[1], 2);
	KUNIT_EXPECT_EQ(test, ctx->free_order[2], 1);
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
	KUNIT_CASE(kunit_resource_test_remove_resource),
	KUNIT_CASE(kunit_resource_test_cleanup_resources),
	KUNIT_CASE(kunit_resource_test_proper_free_ordering),
	KUNIT_CASE(kunit_resource_test_static),
	KUNIT_CASE(kunit_resource_test_named),
	KUNIT_CASE(kunit_resource_test_action),
	KUNIT_CASE(kunit_resource_test_remove_action),
	KUNIT_CASE(kunit_resource_test_release_action),
	KUNIT_CASE(kunit_resource_test_action_ordering),
	{}
};

static struct kunit_suite kunit_resource_test_suite = {
	.name = "kunit-resource-test",
	.init = kunit_resource_test_init,
	.exit = kunit_resource_test_exit,
	.test_cases = kunit_resource_test_cases,
};

/*
 * Log tests call string_stream functions, which aren't exported. So only
 * build this code if this test is built-in.
 */
#if IS_BUILTIN(CONFIG_KUNIT_TEST)

/* This avoids a cast warning if kfree() is passed direct to kunit_add_action(). */
KUNIT_DEFINE_ACTION_WRAPPER(kfree_wrapper, kfree, const void *);

static void kunit_log_test(struct kunit *test)
{
	struct kunit_suite suite;
#ifdef CONFIG_KUNIT_DEBUGFS
	char *full_log;
#endif
	suite.log = kunit_alloc_string_stream(test, GFP_KERNEL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, suite.log);
	string_stream_set_append_newlines(suite.log, true);

	kunit_log(KERN_INFO, test, "put this in log.");
	kunit_log(KERN_INFO, test, "this too.");
	kunit_log(KERN_INFO, &suite, "add to suite log.");
	kunit_log(KERN_INFO, &suite, "along with this.");

#ifdef CONFIG_KUNIT_DEBUGFS
	KUNIT_EXPECT_TRUE(test, test->log->append_newlines);

	full_log = string_stream_get_string(test->log);
	kunit_add_action(test, kfree_wrapper, full_log);
	KUNIT_EXPECT_NOT_ERR_OR_NULL(test,
				     strstr(full_log, "put this in log."));
	KUNIT_EXPECT_NOT_ERR_OR_NULL(test,
				     strstr(full_log, "this too."));

	full_log = string_stream_get_string(suite.log);
	kunit_add_action(test, kfree_wrapper, full_log);
	KUNIT_EXPECT_NOT_ERR_OR_NULL(test,
				     strstr(full_log, "add to suite log."));
	KUNIT_EXPECT_NOT_ERR_OR_NULL(test,
				     strstr(full_log, "along with this."));
#else
	KUNIT_EXPECT_NULL(test, test->log);
#endif
}

static void kunit_log_newline_test(struct kunit *test)
{
	char *full_log;

	kunit_info(test, "Add newline\n");
	if (test->log) {
		full_log = string_stream_get_string(test->log);
		kunit_add_action(test, kfree_wrapper, full_log);
		KUNIT_ASSERT_NOT_NULL_MSG(test, strstr(full_log, "Add newline\n"),
			"Missing log line, full log:\n%s", full_log);
		KUNIT_EXPECT_NULL(test, strstr(full_log, "Add newline\n\n"));
	} else {
		kunit_skip(test, "only useful when debugfs is enabled");
	}
}
#else
static void kunit_log_test(struct kunit *test)
{
	kunit_skip(test, "Log tests only run when built-in");
}

static void kunit_log_newline_test(struct kunit *test)
{
	kunit_skip(test, "Log tests only run when built-in");
}
#endif /* IS_BUILTIN(CONFIG_KUNIT_TEST) */

static struct kunit_case kunit_log_test_cases[] = {
	KUNIT_CASE(kunit_log_test),
	KUNIT_CASE(kunit_log_newline_test),
	{}
};

static struct kunit_suite kunit_log_test_suite = {
	.name = "kunit-log-test",
	.test_cases = kunit_log_test_cases,
};

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

static void kunit_current_test(struct kunit *test)
{
	/* Check results of both current->kunit_test and
	 * kunit_get_current_test() are equivalent to current test.
	 */
	KUNIT_EXPECT_PTR_EQ(test, test, current->kunit_test);
	KUNIT_EXPECT_PTR_EQ(test, test, kunit_get_current_test());
}

static void kunit_current_fail_test(struct kunit *test)
{
	struct kunit fake;

	kunit_init_test(&fake, "fake test", NULL);
	KUNIT_EXPECT_EQ(test, fake.status, KUNIT_SUCCESS);

	/* Set current->kunit_test to fake test. */
	current->kunit_test = &fake;

	kunit_fail_current_test("This should make `fake` test fail.");
	KUNIT_EXPECT_EQ(test, fake.status, (enum kunit_status)KUNIT_FAILURE);
	kunit_cleanup(&fake);

	/* Reset current->kunit_test to current test. */
	current->kunit_test = test;
}

static struct kunit_case kunit_current_test_cases[] = {
	KUNIT_CASE(kunit_current_test),
	KUNIT_CASE(kunit_current_fail_test),
	{}
};

static void test_dev_action(void *priv)
{
	*(void **)priv = (void *)1;
}

static void kunit_device_test(struct kunit *test)
{
	struct device *test_device;
	long action_was_run = 0;

	test_device = kunit_device_register(test, "my_device");
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, test_device);

	// Add an action to verify cleanup.
	devm_add_action(test_device, test_dev_action, &action_was_run);

	KUNIT_EXPECT_EQ(test, action_was_run, 0);

	kunit_device_unregister(test, test_device);

	KUNIT_EXPECT_EQ(test, action_was_run, 1);
}

static void kunit_device_cleanup_test(struct kunit *test)
{
	struct device *test_device;
	long action_was_run = 0;

	test_device = kunit_device_register(test, "my_device");
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, test_device);

	/* Add an action to verify cleanup. */
	devm_add_action(test_device, test_dev_action, &action_was_run);

	KUNIT_EXPECT_EQ(test, action_was_run, 0);

	/* Force KUnit to run cleanup early. */
	kunit_cleanup(test);

	KUNIT_EXPECT_EQ(test, action_was_run, 1);
}

struct driver_test_state {
	bool driver_device_probed;
	bool driver_device_removed;
	long action_was_run;
};

static int driver_probe_hook(struct device *dev)
{
	struct kunit *test = kunit_get_current_test();
	struct driver_test_state *state = (struct driver_test_state *)test->priv;

	state->driver_device_probed = true;
	return 0;
}

static int driver_remove_hook(struct device *dev)
{
	struct kunit *test = kunit_get_current_test();
	struct driver_test_state *state = (struct driver_test_state *)test->priv;

	state->driver_device_removed = true;
	return 0;
}

static void kunit_device_driver_test(struct kunit *test)
{
	struct device_driver *test_driver;
	struct device *test_device;
	struct driver_test_state *test_state = kunit_kzalloc(test, sizeof(*test_state), GFP_KERNEL);

	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, test_state);

	test->priv = test_state;
	test_driver = kunit_driver_create(test, "my_driver");

	// This can fail with an error pointer.
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, test_driver);

	test_driver->probe = driver_probe_hook;
	test_driver->remove = driver_remove_hook;

	test_device = kunit_device_register_with_driver(test, "my_device", test_driver);

	// This can fail with an error pointer.
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, test_device);

	// Make sure the probe function was called.
	KUNIT_ASSERT_TRUE(test, test_state->driver_device_probed);

	// Add an action to verify cleanup.
	devm_add_action(test_device, test_dev_action, &test_state->action_was_run);

	KUNIT_EXPECT_EQ(test, test_state->action_was_run, 0);

	kunit_device_unregister(test, test_device);
	test_device = NULL;

	// Make sure the remove hook was called.
	KUNIT_ASSERT_TRUE(test, test_state->driver_device_removed);

	// We're going to test this again.
	test_state->driver_device_probed = false;

	// The driver should not automatically be destroyed by
	// kunit_device_unregister, so we can re-use it.
	test_device = kunit_device_register_with_driver(test, "my_device", test_driver);

	// This can fail with an error pointer.
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, test_device);

	// Probe was called again.
	KUNIT_ASSERT_TRUE(test, test_state->driver_device_probed);

	// Everything is automatically freed here.
}

static struct kunit_case kunit_device_test_cases[] = {
	KUNIT_CASE(kunit_device_test),
	KUNIT_CASE(kunit_device_cleanup_test),
	KUNIT_CASE(kunit_device_driver_test),
	{}
};

static struct kunit_suite kunit_device_test_suite = {
	.name = "kunit_device",
	.test_cases = kunit_device_test_cases,
};

static struct kunit_suite kunit_current_test_suite = {
	.name = "kunit_current",
	.test_cases = kunit_current_test_cases,
};

kunit_test_suites(&kunit_try_catch_test_suite, &kunit_resource_test_suite,
		  &kunit_log_test_suite, &kunit_status_test_suite,
		  &kunit_current_test_suite, &kunit_device_test_suite,
		  &kunit_fault_test_suite);

MODULE_DESCRIPTION("KUnit test for core test infrastructure");
MODULE_LICENSE("GPL v2");
