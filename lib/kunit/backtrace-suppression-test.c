// SPDX-License-Identifier: GPL-2.0
/*
 * KUnit test for suppressing warning tracebacks.
 *
 * Copyright (C) 2024, Guenter Roeck
 * Author: Guenter Roeck <linux@roeck-us.net>
 */

#include <kunit/test.h>
#include <linux/bug.h>
#include <linux/completion.h>
#include <linux/kthread.h>

static void backtrace_suppression_test_warn_direct(struct kunit *test)
{
	if (!IS_ENABLED(CONFIG_BUG))
		kunit_skip(test, "requires CONFIG_BUG");

	kunit_warning_suppress(test) {
		WARN(1, "This backtrace should be suppressed");
		/*
		 * Count must be checked inside the scope; the handle
		 * is not accessible after the block exits.
		 */
		KUNIT_EXPECT_SUPPRESSED_WARNING_COUNT(test, 1);
	}
	KUNIT_EXPECT_FALSE(test, kunit_has_active_suppress_warning());
}

static noinline void trigger_backtrace_warn(void)
{
	WARN(1, "This backtrace should be suppressed");
}

static void backtrace_suppression_test_warn_indirect(struct kunit *test)
{
	if (!IS_ENABLED(CONFIG_BUG))
		kunit_skip(test, "requires CONFIG_BUG");

	kunit_warning_suppress(test) {
		trigger_backtrace_warn();
		KUNIT_EXPECT_SUPPRESSED_WARNING_COUNT(test, 1);
	}
}

static void backtrace_suppression_test_warn_multi(struct kunit *test)
{
	if (!IS_ENABLED(CONFIG_BUG))
		kunit_skip(test, "requires CONFIG_BUG");

	kunit_warning_suppress(test) {
		WARN(1, "This backtrace should be suppressed");
		trigger_backtrace_warn();
		KUNIT_EXPECT_SUPPRESSED_WARNING_COUNT(test, 2);
	}
}

static void backtrace_suppression_test_warn_on_direct(struct kunit *test)
{
	if (!IS_ENABLED(CONFIG_BUG))
		kunit_skip(test, "requires CONFIG_BUG");
	if (!IS_ENABLED(CONFIG_DEBUG_BUGVERBOSE) && !IS_ENABLED(CONFIG_KALLSYMS))
		kunit_skip(test, "requires CONFIG_DEBUG_BUGVERBOSE or CONFIG_KALLSYMS");

	kunit_warning_suppress(test) {
		WARN_ON(1);
		KUNIT_EXPECT_SUPPRESSED_WARNING_COUNT(test, 1);
	}
}

static noinline void trigger_backtrace_warn_on(void)
{
	WARN_ON(1);
}

static void backtrace_suppression_test_warn_on_indirect(struct kunit *test)
{
	if (!IS_ENABLED(CONFIG_BUG))
		kunit_skip(test, "requires CONFIG_BUG");
	if (!IS_ENABLED(CONFIG_DEBUG_BUGVERBOSE))
		kunit_skip(test, "requires CONFIG_DEBUG_BUGVERBOSE");

	kunit_warning_suppress(test) {
		trigger_backtrace_warn_on();
		KUNIT_EXPECT_SUPPRESSED_WARNING_COUNT(test, 1);
	}
}

static void backtrace_suppression_test_count(struct kunit *test)
{
	if (!IS_ENABLED(CONFIG_BUG))
		kunit_skip(test, "requires CONFIG_BUG");

	kunit_warning_suppress(test) {
		KUNIT_EXPECT_SUPPRESSED_WARNING_COUNT(test, 0);

		WARN(1, "suppressed");
		KUNIT_EXPECT_SUPPRESSED_WARNING_COUNT(test, 1);

		WARN(1, "suppressed again");
		KUNIT_EXPECT_SUPPRESSED_WARNING_COUNT(test, 2);
	}
}

static void backtrace_suppression_test_active_state(struct kunit *test)
{
	KUNIT_EXPECT_FALSE(test, kunit_has_active_suppress_warning());

	kunit_warning_suppress(test) {
		KUNIT_EXPECT_TRUE(test, kunit_has_active_suppress_warning());
	}

	KUNIT_EXPECT_FALSE(test, kunit_has_active_suppress_warning());

	kunit_warning_suppress(test) {
		KUNIT_EXPECT_TRUE(test, kunit_has_active_suppress_warning());
	}

	KUNIT_EXPECT_FALSE(test, kunit_has_active_suppress_warning());
}

static void backtrace_suppression_test_multi_scope(struct kunit *test)
{
	struct kunit_suppressed_warning *sw1, *sw2;

	if (!IS_ENABLED(CONFIG_BUG))
		kunit_skip(test, "requires CONFIG_BUG");
	if (!IS_ENABLED(CONFIG_DEBUG_BUGVERBOSE))
		kunit_skip(test, "requires CONFIG_DEBUG_BUGVERBOSE");

	sw1 = kunit_start_suppress_warning(test);
	trigger_backtrace_warn_on();
	WARN(1, "suppressed by sw1");
	kunit_end_suppress_warning(test, sw1);

	sw2 = kunit_start_suppress_warning(test);
	WARN(1, "suppressed by sw2");
	kunit_end_suppress_warning(test, sw2);

	KUNIT_EXPECT_EQ(test, kunit_suppressed_warning_count(sw1), 2);
	KUNIT_EXPECT_EQ(test, kunit_suppressed_warning_count(sw2), 1);
}

struct cross_kthread_data {
	bool was_active;
	struct completion done;
};

static int cross_kthread_fn(void *data)
{
	struct cross_kthread_data *d = data;

	d->was_active = kunit_has_active_suppress_warning();
	complete(&d->done);
	while (!kthread_should_stop())
		schedule();
	return 0;
}

static void backtrace_suppression_test_cross_kthread(struct kunit *test)
{
	struct cross_kthread_data data;
	struct task_struct *task;

	data.was_active = false;
	init_completion(&data.done);

	kunit_warning_suppress(test) {
		task = kthread_run(cross_kthread_fn, &data, "kunit-cross-test");
		KUNIT_ASSERT_FALSE(test, IS_ERR(task));
		wait_for_completion(&data.done);
		kthread_stop(task);
	}

	KUNIT_EXPECT_FALSE(test, data.was_active);
}

static struct kunit_case backtrace_suppression_test_cases[] = {
	KUNIT_CASE(backtrace_suppression_test_warn_direct),
	KUNIT_CASE(backtrace_suppression_test_warn_indirect),
	KUNIT_CASE(backtrace_suppression_test_warn_multi),
	KUNIT_CASE(backtrace_suppression_test_warn_on_direct),
	KUNIT_CASE(backtrace_suppression_test_warn_on_indirect),
	KUNIT_CASE(backtrace_suppression_test_count),
	KUNIT_CASE(backtrace_suppression_test_active_state),
	KUNIT_CASE(backtrace_suppression_test_multi_scope),
	KUNIT_CASE(backtrace_suppression_test_cross_kthread),
	{}
};

static struct kunit_suite backtrace_suppression_test_suite = {
	.name = "backtrace-suppression-test",
	.test_cases = backtrace_suppression_test_cases,
};
kunit_test_suites(&backtrace_suppression_test_suite);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("KUnit test to verify warning backtrace suppression");
