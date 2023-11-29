// SPDX-License-Identifier: GPL-2.0
/*
 * Base unit test (KUnit) API.
 *
 * Copyright (C) 2019, Google LLC.
 * Author: Brendan Higgins <brendanhiggins@google.com>
 */

#include <kunit/resource.h>
#include <kunit/test.h>
#include <kunit/test-bug.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/panic.h>
#include <linux/sched/debug.h>
#include <linux/sched.h>

#include "debugfs.h"
#include "string-stream.h"
#include "try-catch-impl.h"

#if IS_BUILTIN(CONFIG_KUNIT)
/*
 * Fail the current test and print an error message to the log.
 */
void __kunit_fail_current_test(const char *file, int line, const char *fmt, ...)
{
	va_list args;
	int len;
	char *buffer;

	if (!current->kunit_test)
		return;

	kunit_set_failure(current->kunit_test);

	/* kunit_err() only accepts literals, so evaluate the args first. */
	va_start(args, fmt);
	len = vsnprintf(NULL, 0, fmt, args) + 1;
	va_end(args);

	buffer = kunit_kmalloc(current->kunit_test, len, GFP_KERNEL);
	if (!buffer)
		return;

	va_start(args, fmt);
	vsnprintf(buffer, len, fmt, args);
	va_end(args);

	kunit_err(current->kunit_test, "%s:%d: %s", file, line, buffer);
	kunit_kfree(current->kunit_test, buffer);
}
EXPORT_SYMBOL_GPL(__kunit_fail_current_test);
#endif

/*
 * Enable KUnit tests to run.
 */
#ifdef CONFIG_KUNIT_DEFAULT_ENABLED
static bool enable_param = true;
#else
static bool enable_param;
#endif
module_param_named(enable, enable_param, bool, 0);
MODULE_PARM_DESC(enable, "Enable KUnit tests");

/*
 * KUnit statistic mode:
 * 0 - disabled
 * 1 - only when there is more than one subtest
 * 2 - enabled
 */
static int kunit_stats_enabled = 1;
module_param_named(stats_enabled, kunit_stats_enabled, int, 0644);
MODULE_PARM_DESC(stats_enabled,
		  "Print test stats: never (0), only for multiple subtests (1), or always (2)");

struct kunit_result_stats {
	unsigned long passed;
	unsigned long skipped;
	unsigned long failed;
	unsigned long total;
};

static bool kunit_should_print_stats(struct kunit_result_stats stats)
{
	if (kunit_stats_enabled == 0)
		return false;

	if (kunit_stats_enabled == 2)
		return true;

	return (stats.total > 1);
}

static void kunit_print_test_stats(struct kunit *test,
				   struct kunit_result_stats stats)
{
	if (!kunit_should_print_stats(stats))
		return;

	kunit_log(KERN_INFO, test,
		  KUNIT_SUBTEST_INDENT
		  "# %s: pass:%lu fail:%lu skip:%lu total:%lu",
		  test->name,
		  stats.passed,
		  stats.failed,
		  stats.skipped,
		  stats.total);
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

static void kunit_print_suite_start(struct kunit_suite *suite)
{
	/*
	 * We do not log the test suite header as doing so would
	 * mean debugfs display would consist of the test suite
	 * header prior to individual test results.
	 * Hence directly printk the suite status, and we will
	 * separately seq_printf() the suite header for the debugfs
	 * representation.
	 */
	pr_info(KUNIT_SUBTEST_INDENT "KTAP version 1\n");
	pr_info(KUNIT_SUBTEST_INDENT "# Subtest: %s\n",
		  suite->name);
	pr_info(KUNIT_SUBTEST_INDENT "1..%zd\n",
		  kunit_suite_num_test_cases(suite));
}

static void kunit_print_ok_not_ok(void *test_or_suite,
				  bool is_test,
				  enum kunit_status status,
				  size_t test_number,
				  const char *description,
				  const char *directive)
{
	struct kunit_suite *suite = is_test ? NULL : test_or_suite;
	struct kunit *test = is_test ? test_or_suite : NULL;
	const char *directive_header = (status == KUNIT_SKIPPED) ? " # SKIP " : "";

	/*
	 * We do not log the test suite results as doing so would
	 * mean debugfs display would consist of an incorrect test
	 * number. Hence directly printk the suite result, and we will
	 * separately seq_printf() the suite results for the debugfs
	 * representation.
	 */
	if (suite)
		pr_info("%s %zd %s%s%s\n",
			kunit_status_to_ok_not_ok(status),
			test_number, description, directive_header,
			(status == KUNIT_SKIPPED) ? directive : "");
	else
		kunit_log(KERN_INFO, test,
			  KUNIT_SUBTEST_INDENT "%s %zd %s%s%s",
			  kunit_status_to_ok_not_ok(status),
			  test_number, description, directive_header,
			  (status == KUNIT_SKIPPED) ? directive : "");
}

enum kunit_status kunit_suite_has_succeeded(struct kunit_suite *suite)
{
	const struct kunit_case *test_case;
	enum kunit_status status = KUNIT_SKIPPED;

	if (suite->suite_init_err)
		return KUNIT_FAILURE;

	kunit_suite_for_each_test_case(suite, test_case) {
		if (test_case->status == KUNIT_FAILURE)
			return KUNIT_FAILURE;
		else if (test_case->status == KUNIT_SUCCESS)
			status = KUNIT_SUCCESS;
	}

	return status;
}
EXPORT_SYMBOL_GPL(kunit_suite_has_succeeded);

static size_t kunit_suite_counter = 1;

static void kunit_print_suite_end(struct kunit_suite *suite)
{
	kunit_print_ok_not_ok((void *)suite, false,
			      kunit_suite_has_succeeded(suite),
			      kunit_suite_counter++,
			      suite->name,
			      suite->status_comment);
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

static void kunit_fail(struct kunit *test, const struct kunit_loc *loc,
		       enum kunit_assert_type type, const struct kunit_assert *assert,
		       assert_format_t assert_format, const struct va_format *message)
{
	struct string_stream *stream;

	kunit_set_failure(test);

	stream = alloc_string_stream(test, GFP_KERNEL);
	if (IS_ERR(stream)) {
		WARN(true,
		     "Could not allocate stream to print failed assertion in %s:%d\n",
		     loc->file,
		     loc->line);
		return;
	}

	kunit_assert_prologue(loc, type, stream);
	assert_format(assert, message, stream);

	kunit_print_string_stream(test, stream);

	string_stream_destroy(stream);
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

void kunit_do_failed_assertion(struct kunit *test,
			       const struct kunit_loc *loc,
			       enum kunit_assert_type type,
			       const struct kunit_assert *assert,
			       assert_format_t assert_format,
			       const char *fmt, ...)
{
	va_list args;
	struct va_format message;
	va_start(args, fmt);

	message.fmt = fmt;
	message.va = &args;

	kunit_fail(test, loc, type, assert, assert_format, &message);

	va_end(args);

	if (type == KUNIT_ASSERTION)
		kunit_abort(test);
}
EXPORT_SYMBOL_GPL(kunit_do_failed_assertion);

void kunit_init_test(struct kunit *test, const char *name, char *log)
{
	spin_lock_init(&test->lock);
	INIT_LIST_HEAD(&test->resources);
	test->name = name;
	test->log = log;
	if (test->log)
		test->log[0] = '\0';
	test->status = KUNIT_SUCCESS;
	test->status_comment[0] = '\0';
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

	current->kunit_test = test;

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
					struct kunit_case *test_case,
					struct kunit *test)
{
	struct kunit_try_catch_context context;
	struct kunit_try_catch *try_catch;

	kunit_init_test(test, test_case->name, test_case->log);
	try_catch = &test->try_catch;

	kunit_try_catch_init(try_catch,
			     test,
			     kunit_try_run_case,
			     kunit_catch_run_case);
	context.test = test;
	context.suite = suite;
	context.test_case = test_case;
	kunit_try_catch_run(try_catch, &context);

	/* Propagate the parameter result to the test case. */
	if (test->status == KUNIT_FAILURE)
		test_case->status = KUNIT_FAILURE;
	else if (test_case->status != KUNIT_FAILURE && test->status == KUNIT_SUCCESS)
		test_case->status = KUNIT_SUCCESS;
}

static void kunit_print_suite_stats(struct kunit_suite *suite,
				    struct kunit_result_stats suite_stats,
				    struct kunit_result_stats param_stats)
{
	if (kunit_should_print_stats(suite_stats)) {
		kunit_log(KERN_INFO, suite,
			  "# %s: pass:%lu fail:%lu skip:%lu total:%lu",
			  suite->name,
			  suite_stats.passed,
			  suite_stats.failed,
			  suite_stats.skipped,
			  suite_stats.total);
	}

	if (kunit_should_print_stats(param_stats)) {
		kunit_log(KERN_INFO, suite,
			  "# Totals: pass:%lu fail:%lu skip:%lu total:%lu",
			  param_stats.passed,
			  param_stats.failed,
			  param_stats.skipped,
			  param_stats.total);
	}
}

static void kunit_update_stats(struct kunit_result_stats *stats,
			       enum kunit_status status)
{
	switch (status) {
	case KUNIT_SUCCESS:
		stats->passed++;
		break;
	case KUNIT_SKIPPED:
		stats->skipped++;
		break;
	case KUNIT_FAILURE:
		stats->failed++;
		break;
	}

	stats->total++;
}

static void kunit_accumulate_stats(struct kunit_result_stats *total,
				   struct kunit_result_stats add)
{
	total->passed += add.passed;
	total->skipped += add.skipped;
	total->failed += add.failed;
	total->total += add.total;
}

int kunit_run_tests(struct kunit_suite *suite)
{
	char param_desc[KUNIT_PARAM_DESC_SIZE];
	struct kunit_case *test_case;
	struct kunit_result_stats suite_stats = { 0 };
	struct kunit_result_stats total_stats = { 0 };

	/* Taint the kernel so we know we've run tests. */
	add_taint(TAINT_TEST, LOCKDEP_STILL_OK);

	if (suite->suite_init) {
		suite->suite_init_err = suite->suite_init(suite);
		if (suite->suite_init_err) {
			kunit_err(suite, KUNIT_SUBTEST_INDENT
				  "# failed to initialize (%d)", suite->suite_init_err);
			goto suite_end;
		}
	}

	kunit_print_suite_start(suite);

	kunit_suite_for_each_test_case(suite, test_case) {
		struct kunit test = { .param_value = NULL, .param_index = 0 };
		struct kunit_result_stats param_stats = { 0 };
		test_case->status = KUNIT_SKIPPED;

		if (!test_case->generate_params) {
			/* Non-parameterised test. */
			kunit_run_case_catch_errors(suite, test_case, &test);
			kunit_update_stats(&param_stats, test.status);
		} else {
			/* Get initial param. */
			param_desc[0] = '\0';
			test.param_value = test_case->generate_params(NULL, param_desc);
			kunit_log(KERN_INFO, &test, KUNIT_SUBTEST_INDENT KUNIT_SUBTEST_INDENT
				  "KTAP version 1\n");
			kunit_log(KERN_INFO, &test, KUNIT_SUBTEST_INDENT KUNIT_SUBTEST_INDENT
				  "# Subtest: %s", test_case->name);

			while (test.param_value) {
				kunit_run_case_catch_errors(suite, test_case, &test);

				if (param_desc[0] == '\0') {
					snprintf(param_desc, sizeof(param_desc),
						 "param-%d", test.param_index);
				}

				kunit_log(KERN_INFO, &test,
					  KUNIT_SUBTEST_INDENT KUNIT_SUBTEST_INDENT
					  "%s %d %s",
					  kunit_status_to_ok_not_ok(test.status),
					  test.param_index + 1, param_desc);

				/* Get next param. */
				param_desc[0] = '\0';
				test.param_value = test_case->generate_params(test.param_value, param_desc);
				test.param_index++;

				kunit_update_stats(&param_stats, test.status);
			}
		}


		kunit_print_test_stats(&test, param_stats);

		kunit_print_ok_not_ok(&test, true, test_case->status,
				      kunit_test_case_num(suite, test_case),
				      test_case->name,
				      test.status_comment);

		kunit_update_stats(&suite_stats, test_case->status);
		kunit_accumulate_stats(&total_stats, param_stats);
	}

	if (suite->suite_exit)
		suite->suite_exit(suite);

	kunit_print_suite_stats(suite, suite_stats, total_stats);
suite_end:
	kunit_print_suite_end(suite);

	return 0;
}
EXPORT_SYMBOL_GPL(kunit_run_tests);

static void kunit_init_suite(struct kunit_suite *suite)
{
	kunit_debugfs_create_suite(suite);
	suite->status_comment[0] = '\0';
	suite->suite_init_err = 0;
}

bool kunit_enabled(void)
{
	return enable_param;
}

int __kunit_test_suites_init(struct kunit_suite * const * const suites, int num_suites)
{
	unsigned int i;

	if (!kunit_enabled() && num_suites > 0) {
		pr_info("kunit: disabled\n");
		return 0;
	}

	for (i = 0; i < num_suites; i++) {
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

void __kunit_test_suites_exit(struct kunit_suite **suites, int num_suites)
{
	unsigned int i;

	if (!kunit_enabled())
		return;

	for (i = 0; i < num_suites; i++)
		kunit_exit_suite(suites[i]);

	kunit_suite_counter = 1;
}
EXPORT_SYMBOL_GPL(__kunit_test_suites_exit);

#ifdef CONFIG_MODULES
static void kunit_module_init(struct module *mod)
{
	__kunit_test_suites_init(mod->kunit_suites, mod->num_kunit_suites);
}

static void kunit_module_exit(struct module *mod)
{
	__kunit_test_suites_exit(mod->kunit_suites, mod->num_kunit_suites);
}

static int kunit_module_notify(struct notifier_block *nb, unsigned long val,
			       void *data)
{
	struct module *mod = data;

	switch (val) {
	case MODULE_STATE_LIVE:
		kunit_module_init(mod);
		break;
	case MODULE_STATE_GOING:
		kunit_module_exit(mod);
		break;
	case MODULE_STATE_COMING:
	case MODULE_STATE_UNFORMED:
		break;
	}

	return 0;
}

static struct notifier_block kunit_mod_nb = {
	.notifier_call = kunit_module_notify,
	.priority = 0,
};
#endif

struct kunit_kmalloc_array_params {
	size_t n;
	size_t size;
	gfp_t gfp;
};

static int kunit_kmalloc_array_init(struct kunit_resource *res, void *context)
{
	struct kunit_kmalloc_array_params *params = context;

	res->data = kmalloc_array(params->n, params->size, params->gfp);
	if (!res->data)
		return -ENOMEM;

	return 0;
}

static void kunit_kmalloc_array_free(struct kunit_resource *res)
{
	kfree(res->data);
}

void *kunit_kmalloc_array(struct kunit *test, size_t n, size_t size, gfp_t gfp)
{
	struct kunit_kmalloc_array_params params = {
		.size = size,
		.n = n,
		.gfp = gfp
	};

	return kunit_alloc_resource(test,
				    kunit_kmalloc_array_init,
				    kunit_kmalloc_array_free,
				    gfp,
				    &params);
}
EXPORT_SYMBOL_GPL(kunit_kmalloc_array);

static inline bool kunit_kfree_match(struct kunit *test,
				     struct kunit_resource *res, void *match_data)
{
	/* Only match resources allocated with kunit_kmalloc() and friends. */
	return res->free == kunit_kmalloc_array_free && res->data == match_data;
}

void kunit_kfree(struct kunit *test, const void *ptr)
{
	if (!ptr)
		return;

	if (kunit_destroy_resource(test, kunit_kfree_match, (void *)ptr))
		KUNIT_FAIL(test, "kunit_kfree: %px already freed or not allocated by kunit", ptr);
}
EXPORT_SYMBOL_GPL(kunit_kfree);

void kunit_cleanup(struct kunit *test)
{
	struct kunit_resource *res;
	unsigned long flags;

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
		spin_lock_irqsave(&test->lock, flags);
		if (list_empty(&test->resources)) {
			spin_unlock_irqrestore(&test->lock, flags);
			break;
		}
		res = list_last_entry(&test->resources,
				      struct kunit_resource,
				      node);
		/*
		 * Need to unlock here as a resource may remove another
		 * resource, and this can't happen if the test->lock
		 * is held.
		 */
		spin_unlock_irqrestore(&test->lock, flags);
		kunit_remove_resource(test, res);
	}
	current->kunit_test = NULL;
}
EXPORT_SYMBOL_GPL(kunit_cleanup);

static int __init kunit_init(void)
{
	kunit_debugfs_init();
#ifdef CONFIG_MODULES
	return register_module_notifier(&kunit_mod_nb);
#else
	return 0;
#endif
}
late_initcall(kunit_init);

static void __exit kunit_exit(void)
{
#ifdef CONFIG_MODULES
	unregister_module_notifier(&kunit_mod_nb);
#endif
	kunit_debugfs_cleanup();
}
module_exit(kunit_exit);

MODULE_LICENSE("GPL v2");
