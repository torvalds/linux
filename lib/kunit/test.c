// SPDX-License-Identifier: GPL-2.0
/*
 * Base unit test (KUnit) API.
 *
 * Copyright (C) 2019, Google LLC.
 * Author: Brendan Higgins <brendanhiggins@google.com>
 */

#include <kunit/test.h>
#include <linux/kernel.h>

static void kunit_set_failure(struct kunit *test)
{
	WRITE_ONCE(test->success, false);
}

static int kunit_vprintk_emit(int level, const char *fmt, va_list args)
{
	return vprintk_emit(0, level, NULL, 0, fmt, args);
}

static int kunit_printk_emit(int level, const char *fmt, ...)
{
	va_list args;
	int ret;

	va_start(args, fmt);
	ret = kunit_vprintk_emit(level, fmt, args);
	va_end(args);

	return ret;
}

static void kunit_vprintk(const struct kunit *test,
			  const char *level,
			  struct va_format *vaf)
{
	kunit_printk_emit(level[1] - '0', "\t# %s: %pV", test->name, vaf);
}

static void kunit_print_tap_version(void)
{
	static bool kunit_has_printed_tap_version;

	if (!kunit_has_printed_tap_version) {
		kunit_printk_emit(LOGLEVEL_INFO, "TAP version 14\n");
		kunit_has_printed_tap_version = true;
	}
}

static size_t kunit_test_cases_len(struct kunit_case *test_cases)
{
	struct kunit_case *test_case;
	size_t len = 0;

	for (test_case = test_cases; test_case->run_case; test_case++)
		len++;

	return len;
}

static void kunit_print_subtest_start(struct kunit_suite *suite)
{
	kunit_print_tap_version();
	kunit_printk_emit(LOGLEVEL_INFO, "\t# Subtest: %s\n", suite->name);
	kunit_printk_emit(LOGLEVEL_INFO,
			  "\t1..%zd\n",
			  kunit_test_cases_len(suite->test_cases));
}

static void kunit_print_ok_not_ok(bool should_indent,
				  bool is_ok,
				  size_t test_number,
				  const char *description)
{
	const char *indent, *ok_not_ok;

	if (should_indent)
		indent = "\t";
	else
		indent = "";

	if (is_ok)
		ok_not_ok = "ok";
	else
		ok_not_ok = "not ok";

	kunit_printk_emit(LOGLEVEL_INFO,
			  "%s%s %zd - %s\n",
			  indent, ok_not_ok, test_number, description);
}

static bool kunit_suite_has_succeeded(struct kunit_suite *suite)
{
	const struct kunit_case *test_case;

	for (test_case = suite->test_cases; test_case->run_case; test_case++)
		if (!test_case->success)
			return false;

	return true;
}

static void kunit_print_subtest_end(struct kunit_suite *suite)
{
	static size_t kunit_suite_counter = 1;

	kunit_print_ok_not_ok(false,
			      kunit_suite_has_succeeded(suite),
			      kunit_suite_counter++,
			      suite->name);
}

static void kunit_print_test_case_ok_not_ok(struct kunit_case *test_case,
					    size_t test_number)
{
	kunit_print_ok_not_ok(true,
			      test_case->success,
			      test_number,
			      test_case->name);
}

void kunit_init_test(struct kunit *test, const char *name)
{
	test->name = name;
	test->success = true;
}

/*
 * Performs all logic to run a test case.
 */
static void kunit_run_case(struct kunit_suite *suite,
			   struct kunit_case *test_case)
{
	struct kunit test;

	kunit_init_test(&test, test_case->name);

	if (suite->init) {
		int ret;

		ret = suite->init(&test);
		if (ret) {
			kunit_err(&test, "failed to initialize: %d\n", ret);
			kunit_set_failure(&test);
			test_case->success = test.success;
			return;
		}
	}

	test_case->run_case(&test);

	if (suite->exit)
		suite->exit(&test);

	test_case->success = test.success;
}

int kunit_run_tests(struct kunit_suite *suite)
{
	struct kunit_case *test_case;
	size_t test_case_count = 1;

	kunit_print_subtest_start(suite);

	for (test_case = suite->test_cases; test_case->run_case; test_case++) {
		kunit_run_case(suite, test_case);
		kunit_print_test_case_ok_not_ok(test_case, test_case_count++);
	}

	kunit_print_subtest_end(suite);

	return 0;
}

void kunit_printk(const char *level,
		  const struct kunit *test,
		  const char *fmt, ...)
{
	struct va_format vaf;
	va_list args;

	va_start(args, fmt);

	vaf.fmt = fmt;
	vaf.va = &args;

	kunit_vprintk(test, level, &vaf);

	va_end(args);
}
