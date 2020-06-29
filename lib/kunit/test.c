// SPDX-License-Identifier: GPL-2.0
/*
 * Base unit test (KUnit) API.
 *
 * Copyright (C) 2019, Google LLC.
 * Author: Brendan Higgins <brendanhiggins@google.com>
 */

#include <kunit/test.h>
#include <linux/kernel.h>
#include <linux/sched/debug.h>

#include "debugfs.h"
#include "string-stream.h"
#include "try-catch-impl.h"

static void kunit_set_failure(struct kunit *test)
{
	WRITE_ONCE(test->success, false);
}

static void kunit_print_tap_version(void)
{
	static bool kunit_has_printed_tap_version;

	if (!kunit_has_printed_tap_version) {
		pr_info("TAP version 14\n");
		kunit_has_printed_tap_version = true;
	}
}

/*
 * Append formatted message to log, size of which is limited to
 * KUNIT_LOG_SIZE bytes (including null terminating byte).
 */
void kunit_log_append(char *log, const char *fmt, ...)
{
	char line[KUNIT_LOG_SIZE];
	va_list args;
	int len_left;

	if (!log)
		return;

	len_left = KUNIT_LOG_SIZE - strlen(log) - 1;
	if (len_left <= 0)
		return;

	va_start(args, fmt);
	vsnprintf(line, sizeof(line), fmt, args);
	va_end(args);

	strncat(log, line, len_left);
}
EXPORT_SYMBOL_GPL(kunit_log_append);

size_t kunit_suite_num_test_cases(struct kunit_suite *suite)
{
	struct kunit_case *test_case;
	size_t len = 0;

	kunit_suite_for_each_test_case(suite, test_case)
		len++;

	return len;
}
EXPORT_SYMBOL_GPL(kunit_suite_num_test_cases);

static void kunit_print_subtest_start(struct kunit_suite *suite)
{
	kunit_print_tap_version();
	kunit_log(KERN_INFO, suite, KUNIT_SUBTEST_INDENT "# Subtest: %s",
		  suite->name);
	kunit_log(KERN_INFO, suite, KUNIT_SUBTEST_INDENT "1..%zd",
		  kunit_suite_num_test_cases(suite));
}

static void kunit_print_ok_not_ok(void *test_or_suite,
				  bool is_test,
				  bool is_ok,
				  size_t test_number,
				  const char *description)
{
	struct kunit_suite *suite = is_test ? NULL : test_or_suite;
	struct kunit *test = is_test ? test_or_suite : NULL;

	/*
	 * We do not log the test suite results as doing so would
	 * mean debugfs display would consist of the test suite
	 * description and status prior to individual test results.
	 * Hence directly printk the suite status, and we will
	 * separately seq_printf() the suite status for the debugfs
	 * representation.
	 */
	if (suite)
		pr_info("%s %zd - %s\n",
			kunit_status_to_string(is_ok),
			test_number, description);
	else
		kunit_log(KERN_INFO, test, KUNIT_SUBTEST_INDENT "%s %zd - %s",
			  kunit_status_to_string(is_ok),
			  test_number, description);
}

bool kunit_suite_has_succeeded(struct kunit_suite *suite)
{
	const struct kunit_case *test_case;

	kunit_suite_for_each_test_case(suite, test_case) {
		if (!test_case->success)
			return false;
	}

	return true;
}
EXPORT_SYMBOL_GPL(kunit_suite_has_succeeded);

static void kunit_print_subtest_end(struct kunit_suite *suite)
{
	static size_t kunit_suite_counter = 1;

	kunit_print_ok_not_ok((void *)suite, false,
			      kunit_suite_has_succeeded(suite),
			      kunit_suite_counter++,
			      suite->name);
}

unsigned int kunit_test_case_num(struct kunit_suite *suite,
				 struct kunit_case *test_case)
{
	struct kunit_case *tc;
	unsigned int i = 1;

	kunit_suite_for_each_test_case(suite, tc) {
		if (tc == test_case)
			return i;
		i++;
	}

	return 0;
}
EXPORT_SYMBOL_GPL(kunit_test_case_num);

static void kunit_print_string_stream(struct kunit *test,
				      struct string_stream *stream)
{
	struct string_stream_fragment *fragment;
	char *buf;

	if (string_stream_is_empty(stream))
		return;

	buf = string_stream_get_string(stream);
	if (!buf) {
		kunit_err(test,
			  "Could not allocate buffer, dumping stream:\n");
		list_for_each_entry(fragment, &stream->fragments, node) {
			kunit_err(test, "%s", fragment->fragment);
		}
		kunit_err(test, "\n");
	} else {
		kunit_err(test, "%s", buf);
		kunit_kfree(test, buf);
	}
}

static void kunit_fail(struct kunit *test, struct kunit_assert *assert)
{
	struct string_stream *stream;

	kunit_set_failure(test);

	stream = alloc_string_stream(test, GFP_KERNEL);
	if (!stream) {
		WARN(true,
		     "Could not allocate stream to print failed assertion in %s:%d\n",
		     assert->file,
		     assert->line);
		return;
	}

	assert->format(assert, stream);

	kunit_print_string_stream(test, stream);

	WARN_ON(string_stream_destroy(stream));
}

static void __noreturn kunit_abort(struct kunit *test)
{
	kunit_try_catch_throw(&test->try_catch); /* Does not return. */

	/*
	 * Throw could not abort from test.
	 *
	 * XXX: we should never reach this line! As kunit_try_catch_throw is
	 * marked __noreturn.
	 */
	WARN_ONCE(true, "Throw could not abort from test!\n");
}

void kunit_do_assertion(struct kunit *test,
			struct kunit_assert *assert,
			bool pass,
			const char *fmt, ...)
{
	va_list args;

	if (pass)
		return;

	va_start(args, fmt);

	assert->message.fmt = fmt;
	assert->message.va = &args;

	kunit_fail(test, assert);

	va_end(args);

	if (assert->type == KUNIT_ASSERTION)
		kunit_abort(test);
}
EXPORT_SYMBOL_GPL(kunit_do_assertion);

void kunit_init_test(struct kunit *test, const char *name, char *log)
{
	spin_lock_init(&test->lock);
	INIT_LIST_HEAD(&test->resources);
	test->name = name;
	test->log = log;
	if (test->log)
		test->log[0] = '\0';
	test->success = true;
}
EXPORT_SYMBOL_GPL(kunit_init_test);

/*
 * Initializes and runs test case. Does not clean up or do post validations.
 */
static void kunit_run_case_internal(struct kunit *test,
				    struct kunit_suite *suite,
				    struct kunit_case *test_case)
{
	if (suite->init) {
		int ret;

		ret = suite->init(test);
		if (ret) {
			kunit_err(test, "failed to initialize: %d\n", ret);
			kunit_set_failure(test);
			return;
		}
	}

	test_case->run_case(test);
}

static void kunit_case_internal_cleanup(struct kunit *test)
{
	kunit_cleanup(test);
}

/*
 * Performs post validations and cleanup after a test case was run.
 * XXX: Should ONLY BE CALLED AFTER kunit_run_case_internal!
 */
static void kunit_run_case_cleanup(struct kunit *test,
				   struct kunit_suite *suite)
{
	if (suite->exit)
		suite->exit(test);

	kunit_case_internal_cleanup(test);
}

struct kunit_try_catch_context {
	struct kunit *test;
	struct kunit_suite *suite;
	struct kunit_case *test_case;
};

static void kunit_try_run_case(void *data)
{
	struct kunit_try_catch_context *ctx = data;
	struct kunit *test = ctx->test;
	struct kunit_suite *suite = ctx->suite;
	struct kunit_case *test_case = ctx->test_case;

	/*
	 * kunit_run_case_internal may encounter a fatal error; if it does,
	 * abort will be called, this thread will exit, and finally the parent
	 * thread will resume control and handle any necessary clean up.
	 */
	kunit_run_case_internal(test, suite, test_case);
	/* This line may never be reached. */
	kunit_run_case_cleanup(test, suite);
}

static void kunit_catch_run_case(void *data)
{
	struct kunit_try_catch_context *ctx = data;
	struct kunit *test = ctx->test;
	struct kunit_suite *suite = ctx->suite;
	int try_exit_code = kunit_try_catch_get_result(&test->try_catch);

	if (try_exit_code) {
		kunit_set_failure(test);
		/*
		 * Test case could not finish, we have no idea what state it is
		 * in, so don't do clean up.
		 */
		if (try_exit_code == -ETIMEDOUT) {
			kunit_err(test, "test case timed out\n");
		/*
		 * Unknown internal error occurred preventing test case from
		 * running, so there is nothing to clean up.
		 */
		} else {
			kunit_err(test, "internal error occurred preventing test case from running: %d\n",
				  try_exit_code);
		}
		return;
	}

	/*
	 * Test case was run, but aborted. It is the test case's business as to
	 * whether it failed or not, we just need to clean up.
	 */
	kunit_run_case_cleanup(test, suite);
}

/*
 * Performs all logic to run a test case. It also catches most errors that
 * occur in a test case and reports them as failures.
 */
static void kunit_run_case_catch_errors(struct kunit_suite *suite,
					struct kunit_case *test_case)
{
	struct kunit_try_catch_context context;
	struct kunit_try_catch *try_catch;
	struct kunit test;

	kunit_init_test(&test, test_case->name, test_case->log);
	try_catch = &test.try_catch;

	kunit_try_catch_init(try_catch,
			     &test,
			     kunit_try_run_case,
			     kunit_catch_run_case);
	context.test = &test;
	context.suite = suite;
	context.test_case = test_case;
	kunit_try_catch_run(try_catch, &context);

	test_case->success = test.success;

	kunit_print_ok_not_ok(&test, true, test_case->success,
			      kunit_test_case_num(suite, test_case),
			      test_case->name);
}

int kunit_run_tests(struct kunit_suite *suite)
{
	struct kunit_case *test_case;

	kunit_print_subtest_start(suite);

	kunit_suite_for_each_test_case(suite, test_case)
		kunit_run_case_catch_errors(suite, test_case);

	kunit_print_subtest_end(suite);

	return 0;
}
EXPORT_SYMBOL_GPL(kunit_run_tests);

static void kunit_init_suite(struct kunit_suite *suite)
{
	kunit_debugfs_create_suite(suite);
}

int __kunit_test_suites_init(struct kunit_suite **suites)
{
	unsigned int i;

	for (i = 0; suites[i] != NULL; i++) {
		kunit_init_suite(suites[i]);
		kunit_run_tests(suites[i]);
	}
	return 0;
}
EXPORT_SYMBOL_GPL(__kunit_test_suites_init);

static void kunit_exit_suite(struct kunit_suite *suite)
{
	kunit_debugfs_destroy_suite(suite);
}

void __kunit_test_suites_exit(struct kunit_suite **suites)
{
	unsigned int i;

	for (i = 0; suites[i] != NULL; i++)
		kunit_exit_suite(suites[i]);
}
EXPORT_SYMBOL_GPL(__kunit_test_suites_exit);

struct kunit_resource *kunit_alloc_and_get_resource(struct kunit *test,
						    kunit_resource_init_t init,
						    kunit_resource_free_t free,
						    gfp_t internal_gfp,
						    void *context)
{
	struct kunit_resource *res;
	int ret;

	res = kzalloc(sizeof(*res), internal_gfp);
	if (!res)
		return NULL;

	ret = init(res, context);
	if (ret)
		return NULL;

	res->free = free;
	spin_lock(&test->lock);
	list_add_tail(&res->node, &test->resources);
	spin_unlock(&test->lock);

	return res;
}
EXPORT_SYMBOL_GPL(kunit_alloc_and_get_resource);

static void kunit_resource_free(struct kunit *test, struct kunit_resource *res)
{
	res->free(res);
	kfree(res);
}

static struct kunit_resource *kunit_resource_find(struct kunit *test,
						  kunit_resource_match_t match,
						  kunit_resource_free_t free,
						  void *match_data)
{
	struct kunit_resource *resource;

	lockdep_assert_held(&test->lock);

	list_for_each_entry_reverse(resource, &test->resources, node) {
		if (resource->free != free)
			continue;
		if (match(test, resource->allocation, match_data))
			return resource;
	}

	return NULL;
}

static struct kunit_resource *kunit_resource_remove(
		struct kunit *test,
		kunit_resource_match_t match,
		kunit_resource_free_t free,
		void *match_data)
{
	struct kunit_resource *resource;

	spin_lock(&test->lock);
	resource = kunit_resource_find(test, match, free, match_data);
	if (resource)
		list_del(&resource->node);
	spin_unlock(&test->lock);

	return resource;
}

int kunit_resource_destroy(struct kunit *test,
			   kunit_resource_match_t match,
			   kunit_resource_free_t free,
			   void *match_data)
{
	struct kunit_resource *resource;

	resource = kunit_resource_remove(test, match, free, match_data);

	if (!resource)
		return -ENOENT;

	kunit_resource_free(test, resource);
	return 0;
}
EXPORT_SYMBOL_GPL(kunit_resource_destroy);

struct kunit_kmalloc_params {
	size_t size;
	gfp_t gfp;
};

static int kunit_kmalloc_init(struct kunit_resource *res, void *context)
{
	struct kunit_kmalloc_params *params = context;

	res->allocation = kmalloc(params->size, params->gfp);
	if (!res->allocation)
		return -ENOMEM;

	return 0;
}

static void kunit_kmalloc_free(struct kunit_resource *res)
{
	kfree(res->allocation);
}

void *kunit_kmalloc(struct kunit *test, size_t size, gfp_t gfp)
{
	struct kunit_kmalloc_params params = {
		.size = size,
		.gfp = gfp
	};

	return kunit_alloc_resource(test,
				    kunit_kmalloc_init,
				    kunit_kmalloc_free,
				    gfp,
				    &params);
}
EXPORT_SYMBOL_GPL(kunit_kmalloc);

void kunit_kfree(struct kunit *test, const void *ptr)
{
	int rc;

	rc = kunit_resource_destroy(test,
				    kunit_resource_instance_match,
				    kunit_kmalloc_free,
				    (void *)ptr);

	WARN_ON(rc);
}
EXPORT_SYMBOL_GPL(kunit_kfree);

void kunit_cleanup(struct kunit *test)
{
	struct kunit_resource *resource;

	/*
	 * test->resources is a stack - each allocation must be freed in the
	 * reverse order from which it was added since one resource may depend
	 * on another for its entire lifetime.
	 * Also, we cannot use the normal list_for_each constructs, even the
	 * safe ones because *arbitrary* nodes may be deleted when
	 * kunit_resource_free is called; the list_for_each_safe variants only
	 * protect against the current node being deleted, not the next.
	 */
	while (true) {
		spin_lock(&test->lock);
		if (list_empty(&test->resources)) {
			spin_unlock(&test->lock);
			break;
		}
		resource = list_last_entry(&test->resources,
					   struct kunit_resource,
					   node);
		list_del(&resource->node);
		spin_unlock(&test->lock);

		kunit_resource_free(test, resource);
	}
}
EXPORT_SYMBOL_GPL(kunit_cleanup);

static int __init kunit_init(void)
{
	kunit_debugfs_init();

	return 0;
}
late_initcall(kunit_init);

static void __exit kunit_exit(void)
{
	kunit_debugfs_cleanup();
}
module_exit(kunit_exit);

MODULE_LICENSE("GPL v2");
