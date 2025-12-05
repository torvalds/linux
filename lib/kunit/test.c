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
#include <kunit/attributes.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/mutex.h>
#include <linux/panic.h>
#include <linux/sched/debug.h>
#include <linux/sched.h>
#include <linux/mm.h>

#include "debugfs.h"
#include "device-impl.h"
#include "hooks-impl.h"
#include "string-stream.h"
#include "try-catch-impl.h"

static DEFINE_MUTEX(kunit_run_lock);

/*
 * Hook to fail the current test and print an error message to the log.
 */
void __printf(3, 4) __kunit_fail_current_test_impl(const char *file, int line, const char *fmt, ...)
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
 * Configure the base timeout.
 */
static unsigned long kunit_base_timeout = CONFIG_KUNIT_DEFAULT_TIMEOUT;
module_param_named(timeout, kunit_base_timeout, ulong, 0644);
MODULE_PARM_DESC(timeout, "Set the base timeout for Kunit test cases");

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

/* Append formatted message to log. */
void kunit_log_append(struct string_stream *log, const char *fmt, ...)
{
	va_list args;

	if (!log)
		return;

	va_start(args, fmt);
	string_stream_vadd(log, fmt, args);
	va_end(args);
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

/* Currently supported test levels */
enum {
	KUNIT_LEVEL_SUITE = 0,
	KUNIT_LEVEL_CASE,
	KUNIT_LEVEL_CASE_PARAM,
};

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
	kunit_print_attr((void *)suite, false, KUNIT_LEVEL_CASE);
	pr_info(KUNIT_SUBTEST_INDENT "1..%zd\n",
		  kunit_suite_num_test_cases(suite));
}

static void kunit_print_ok_not_ok(struct kunit *test,
				  unsigned int test_level,
				  enum kunit_status status,
				  size_t test_number,
				  const char *description,
				  const char *directive)
{
	const char *directive_header = (status == KUNIT_SKIPPED) ? " # SKIP " : "";
	const char *directive_body = (status == KUNIT_SKIPPED) ? directive : "";

	/*
	 * When test is NULL assume that results are from the suite
	 * and today suite results are expected at level 0 only.
	 */
	WARN(!test && test_level, "suite test level can't be %u!\n", test_level);

	/*
	 * We do not log the test suite results as doing so would
	 * mean debugfs display would consist of an incorrect test
	 * number. Hence directly printk the suite result, and we will
	 * separately seq_printf() the suite results for the debugfs
	 * representation.
	 */
	if (!test)
		pr_info("%s %zd %s%s%s\n",
			kunit_status_to_ok_not_ok(status),
			test_number, description, directive_header,
			directive_body);
	else
		kunit_log(KERN_INFO, test,
			  "%*s%s %zd %s%s%s",
			  KUNIT_INDENT_LEN * test_level, "",
			  kunit_status_to_ok_not_ok(status),
			  test_number, description, directive_header,
			  directive_body);
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
	kunit_print_ok_not_ok(NULL, KUNIT_LEVEL_SUITE,
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
		kfree(buf);
	}
}

static void kunit_fail(struct kunit *test, const struct kunit_loc *loc,
		       enum kunit_assert_type type, const struct kunit_assert *assert,
		       assert_format_t assert_format, const struct va_format *message)
{
	struct string_stream *stream;

	kunit_set_failure(test);

	stream = kunit_alloc_string_stream(test, GFP_KERNEL);
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

	kunit_free_string_stream(test, stream);
}

void __noreturn __kunit_abort(struct kunit *test)
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
EXPORT_SYMBOL_GPL(__kunit_abort);

void __kunit_do_failed_assertion(struct kunit *test,
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
}
EXPORT_SYMBOL_GPL(__kunit_do_failed_assertion);

static void kunit_init_params(struct kunit *test)
{
	test->params_array.params = NULL;
	test->params_array.get_description = NULL;
	test->params_array.num_params = 0;
	test->params_array.elem_size = 0;
}

void kunit_init_test(struct kunit *test, const char *name, struct string_stream *log)
{
	spin_lock_init(&test->lock);
	INIT_LIST_HEAD(&test->resources);
	test->name = name;
	test->log = log;
	if (test->log)
		string_stream_clear(log);
	test->status = KUNIT_SUCCESS;
	test->status_comment[0] = '\0';
	kunit_init_params(test);
}
EXPORT_SYMBOL_GPL(kunit_init_test);

/* Only warn when a test takes more than twice the threshold */
#define KUNIT_SPEED_WARNING_MULTIPLIER	2

/* Slow tests are defined as taking more than 1s */
#define KUNIT_SPEED_SLOW_THRESHOLD_S	1

#define KUNIT_SPEED_SLOW_WARNING_THRESHOLD_S	\
	(KUNIT_SPEED_WARNING_MULTIPLIER * KUNIT_SPEED_SLOW_THRESHOLD_S)

#define s_to_timespec64(s) ns_to_timespec64((s) * NSEC_PER_SEC)

static void kunit_run_case_check_speed(struct kunit *test,
				       struct kunit_case *test_case,
				       struct timespec64 duration)
{
	struct timespec64 slow_thr =
		s_to_timespec64(KUNIT_SPEED_SLOW_WARNING_THRESHOLD_S);
	enum kunit_speed speed = test_case->attr.speed;

	if (timespec64_compare(&duration, &slow_thr) < 0)
		return;

	if (speed == KUNIT_SPEED_VERY_SLOW || speed == KUNIT_SPEED_SLOW)
		return;

	kunit_warn(test,
		   "Test should be marked slow (runtime: %lld.%09lds)",
		   duration.tv_sec, duration.tv_nsec);
}

/* Returns timeout multiplier based on speed.
 * DEFAULT:		    1
 * KUNIT_SPEED_SLOW:        3
 * KUNIT_SPEED_VERY_SLOW:   12
 */
static int kunit_timeout_mult(enum kunit_speed speed)
{
	switch (speed) {
	case KUNIT_SPEED_SLOW:
		return 3;
	case KUNIT_SPEED_VERY_SLOW:
		return 12;
	default:
		return 1;
	}
}

static unsigned long kunit_test_timeout(struct kunit_suite *suite, struct kunit_case *test_case)
{
	int mult = 1;

	/*
	 * The default test timeout is 300 seconds and will be adjusted by mult
	 * based on the test speed. The test speed will be overridden by the
	 * innermost test component.
	 */
	if (suite->attr.speed != KUNIT_SPEED_UNSET)
		mult = kunit_timeout_mult(suite->attr.speed);
	if (test_case->attr.speed != KUNIT_SPEED_UNSET)
		mult = kunit_timeout_mult(test_case->attr.speed);
	return mult * kunit_base_timeout * msecs_to_jiffies(MSEC_PER_SEC);
}


/*
 * Initializes and runs test case. Does not clean up or do post validations.
 */
static void kunit_run_case_internal(struct kunit *test,
				    struct kunit_suite *suite,
				    struct kunit_case *test_case)
{
	struct timespec64 start, end;

	if (suite->init) {
		int ret;

		ret = suite->init(test);
		if (ret) {
			kunit_err(test, "failed to initialize: %d\n", ret);
			kunit_set_failure(test);
			return;
		}
	}

	ktime_get_ts64(&start);

	test_case->run_case(test);

	ktime_get_ts64(&end);

	kunit_run_case_check_speed(test, test_case, timespec64_sub(end, start));
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
}

static void kunit_try_run_case_cleanup(void *data)
{
	struct kunit_try_catch_context *ctx = data;
	struct kunit *test = ctx->test;
	struct kunit_suite *suite = ctx->suite;

	current->kunit_test = test;

	kunit_run_case_cleanup(test, suite);
}

static void kunit_catch_run_case_cleanup(void *data)
{
	struct kunit_try_catch_context *ctx = data;
	struct kunit *test = ctx->test;
	int try_exit_code = kunit_try_catch_get_result(&test->try_catch);

	/* It is always a failure if cleanup aborts. */
	kunit_set_failure(test);

	if (try_exit_code) {
		/*
		 * Test case could not finish, we have no idea what state it is
		 * in, so don't do clean up.
		 */
		if (try_exit_code == -ETIMEDOUT) {
			kunit_err(test, "test case cleanup timed out\n");
		/*
		 * Unknown internal error occurred preventing test case from
		 * running, so there is nothing to clean up.
		 */
		} else {
			kunit_err(test, "internal error occurred during test case cleanup: %d\n",
				  try_exit_code);
		}
		return;
	}

	kunit_err(test, "test aborted during cleanup. continuing without cleaning up\n");
}


static void kunit_catch_run_case(void *data)
{
	struct kunit_try_catch_context *ctx = data;
	struct kunit *test = ctx->test;
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

	try_catch = &test->try_catch;

	kunit_try_catch_init(try_catch,
			     test,
			     kunit_try_run_case,
			     kunit_catch_run_case,
			     kunit_test_timeout(suite, test_case));
	context.test = test;
	context.suite = suite;
	context.test_case = test_case;
	kunit_try_catch_run(try_catch, &context);

	/* Now run the cleanup */
	kunit_try_catch_init(try_catch,
			     test,
			     kunit_try_run_case_cleanup,
			     kunit_catch_run_case_cleanup,
			     kunit_test_timeout(suite, test_case));
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

const void *kunit_array_gen_params(struct kunit *test, const void *prev, char *desc)
{
	struct kunit_params *params_arr = &test->params_array;
	const void *param;

	if (test->param_index < params_arr->num_params) {
		param = (char *)params_arr->params
			+ test->param_index * params_arr->elem_size;

		if (params_arr->get_description)
			params_arr->get_description(test, param, desc);
		return param;
	}
	return NULL;
}
EXPORT_SYMBOL_GPL(kunit_array_gen_params);

static void kunit_init_parent_param_test(struct kunit_case *test_case, struct kunit *test)
{
	if (test_case->param_init) {
		int err = test_case->param_init(test);

		if (err) {
			kunit_err(test_case, KUNIT_SUBTEST_INDENT KUNIT_SUBTEST_INDENT
				"# failed to initialize parent parameter test (%d)", err);
			test->status = KUNIT_FAILURE;
			test_case->status = KUNIT_FAILURE;
		}
	}
}

int kunit_run_tests(struct kunit_suite *suite)
{
	char param_desc[KUNIT_PARAM_DESC_SIZE];
	struct kunit_case *test_case;
	struct kunit_result_stats suite_stats = { 0 };
	struct kunit_result_stats total_stats = { 0 };
	const void *curr_param;

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

		kunit_init_test(&test, test_case->name, test_case->log);
		if (test_case->status == KUNIT_SKIPPED) {
			/* Test marked as skip */
			test.status = KUNIT_SKIPPED;
			kunit_update_stats(&param_stats, test.status);
		} else if (!test_case->generate_params) {
			/* Non-parameterised test. */
			test_case->status = KUNIT_SKIPPED;
			kunit_run_case_catch_errors(suite, test_case, &test);
			kunit_update_stats(&param_stats, test.status);
		} else {
			kunit_init_parent_param_test(test_case, &test);
			if (test_case->status == KUNIT_FAILURE) {
				kunit_update_stats(&param_stats, test.status);
				goto test_case_end;
			}
			/* Get initial param. */
			param_desc[0] = '\0';
			/* TODO: Make generate_params try-catch */
			curr_param = test_case->generate_params(&test, NULL, param_desc);
			test_case->status = KUNIT_SKIPPED;
			kunit_log(KERN_INFO, &test, KUNIT_SUBTEST_INDENT KUNIT_SUBTEST_INDENT
				  "KTAP version 1\n");
			kunit_log(KERN_INFO, &test, KUNIT_SUBTEST_INDENT KUNIT_SUBTEST_INDENT
				  "# Subtest: %s", test_case->name);
			if (test.params_array.params &&
			    test_case->generate_params == kunit_array_gen_params) {
				kunit_log(KERN_INFO, &test, KUNIT_SUBTEST_INDENT
					  KUNIT_SUBTEST_INDENT "1..%zd\n",
					  test.params_array.num_params);
			}

			while (curr_param) {
				struct kunit param_test = {
					.param_value = curr_param,
					.param_index = ++test.param_index,
					.parent = &test,
				};
				kunit_init_test(&param_test, test_case->name, NULL);
				param_test.log = test_case->log;
				kunit_run_case_catch_errors(suite, test_case, &param_test);

				if (param_desc[0] == '\0') {
					snprintf(param_desc, sizeof(param_desc),
						 "param-%d", param_test.param_index);
				}

				kunit_print_ok_not_ok(&param_test, KUNIT_LEVEL_CASE_PARAM,
						      param_test.status,
						      param_test.param_index,
						      param_desc,
						      param_test.status_comment);

				kunit_update_stats(&param_stats, param_test.status);

				/* Get next param. */
				param_desc[0] = '\0';
				curr_param = test_case->generate_params(&test, curr_param,
									param_desc);
			}
			/*
			 * TODO: Put into a try catch. Since we don't need suite->exit
			 * for it we can't reuse kunit_try_run_cleanup for this yet.
			 */
			if (test_case->param_exit)
				test_case->param_exit(&test);
			/* TODO: Put this kunit_cleanup into a try-catch. */
			kunit_cleanup(&test);
		}
test_case_end:
		kunit_print_attr((void *)test_case, true, KUNIT_LEVEL_CASE);

		kunit_print_test_stats(&test, param_stats);

		kunit_print_ok_not_ok(&test, KUNIT_LEVEL_CASE, test_case->status,
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

	if (suite->log)
		string_stream_clear(suite->log);
}

bool kunit_enabled(void)
{
	return enable_param;
}

int __kunit_test_suites_init(struct kunit_suite * const * const suites, int num_suites,
			     bool run_tests)
{
	unsigned int i;

	if (num_suites == 0)
		return 0;

	if (!kunit_enabled() && num_suites > 0) {
		pr_info("kunit: disabled\n");
		return 0;
	}

	kunit_suite_counter = 1;

	/* Use mutex lock to guard against running tests concurrently. */
	if (mutex_lock_interruptible(&kunit_run_lock)) {
		pr_err("kunit: test interrupted\n");
		return -EINTR;
	}
	static_branch_inc(&kunit_running);

	for (i = 0; i < num_suites; i++) {
		kunit_init_suite(suites[i]);
		if (run_tests)
			kunit_run_tests(suites[i]);
	}

	static_branch_dec(&kunit_running);
	mutex_unlock(&kunit_run_lock);
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
}
EXPORT_SYMBOL_GPL(__kunit_test_suites_exit);

static void kunit_module_init(struct module *mod)
{
	struct kunit_suite_set suite_set, filtered_set;
	struct kunit_suite_set normal_suite_set = {
		mod->kunit_suites, mod->kunit_suites + mod->num_kunit_suites,
	};
	struct kunit_suite_set init_suite_set = {
		mod->kunit_init_suites, mod->kunit_init_suites + mod->num_kunit_init_suites,
	};
	const char *action = kunit_action();
	int err = 0;

	if (mod->num_kunit_init_suites > 0)
		suite_set = kunit_merge_suite_sets(init_suite_set, normal_suite_set);
	else
		suite_set = normal_suite_set;

	filtered_set = kunit_filter_suites(&suite_set,
					kunit_filter_glob() ?: "*.*",
					kunit_filter(), kunit_filter_action(),
					&err);
	if (err)
		pr_err("kunit module: error filtering suites: %d\n", err);

	mod->kunit_suites = (struct kunit_suite **)filtered_set.start;
	mod->num_kunit_suites = filtered_set.end - filtered_set.start;

	if (mod->num_kunit_init_suites > 0)
		kfree(suite_set.start);

	if (!action)
		kunit_exec_run_tests(&filtered_set, false);
	else if (!strcmp(action, "list"))
		kunit_exec_list_tests(&filtered_set, false);
	else if (!strcmp(action, "list_attr"))
		kunit_exec_list_tests(&filtered_set, true);
	else
		pr_err("kunit: unknown action '%s'\n", action);
}

static void kunit_module_exit(struct module *mod)
{
	struct kunit_suite_set suite_set = {
		mod->kunit_suites, mod->kunit_suites + mod->num_kunit_suites,
	};
	const char *action = kunit_action();

	/*
	 * Check if the start address is a valid virtual address to detect
	 * if the module load sequence has failed and the suite set has not
	 * been initialized and filtered.
	 */
	if (!suite_set.start || !virt_addr_valid(suite_set.start))
		return;

	if (!action)
		__kunit_test_suites_exit(mod->kunit_suites,
					 mod->num_kunit_suites);

	kunit_free_suite_set(suite_set);
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
		break;
	case MODULE_STATE_UNFORMED:
		break;
	}

	return 0;
}

static struct notifier_block kunit_mod_nb = {
	.notifier_call = kunit_module_notify,
	.priority = 0,
};

KUNIT_DEFINE_ACTION_WRAPPER(kfree_action_wrapper, kfree, const void *)

void *kunit_kmalloc_array(struct kunit *test, size_t n, size_t size, gfp_t gfp)
{
	void *data;

	data = kmalloc_array(n, size, gfp);

	if (!data)
		return NULL;

	if (kunit_add_action_or_reset(test, kfree_action_wrapper, data) != 0)
		return NULL;

	return data;
}
EXPORT_SYMBOL_GPL(kunit_kmalloc_array);

void kunit_kfree(struct kunit *test, const void *ptr)
{
	if (!ptr)
		return;

	kunit_release_action(test, kfree_action_wrapper, (void *)ptr);
}
EXPORT_SYMBOL_GPL(kunit_kfree);

void kunit_kfree_const(struct kunit *test, const void *x)
{
#if !IS_MODULE(CONFIG_KUNIT)
	if (!is_kernel_rodata((unsigned long)x))
#endif
		kunit_kfree(test, x);
}
EXPORT_SYMBOL_GPL(kunit_kfree_const);

const char *kunit_kstrdup_const(struct kunit *test, const char *str, gfp_t gfp)
{
#if !IS_MODULE(CONFIG_KUNIT)
	if (is_kernel_rodata((unsigned long)str))
		return str;
#endif
	return kunit_kstrdup(test, str, gfp);
}
EXPORT_SYMBOL_GPL(kunit_kstrdup_const);

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
	/* Install the KUnit hook functions. */
	kunit_install_hooks();

	kunit_debugfs_init();

	kunit_bus_init();
	return register_module_notifier(&kunit_mod_nb);
}
late_initcall(kunit_init);

static void __exit kunit_exit(void)
{
	memset(&kunit_hooks, 0, sizeof(kunit_hooks));
	unregister_module_notifier(&kunit_mod_nb);

	kunit_bus_shutdown();

	kunit_debugfs_cleanup();
}
module_exit(kunit_exit);

MODULE_DESCRIPTION("Base unit test (KUnit) API");
MODULE_LICENSE("GPL v2");
