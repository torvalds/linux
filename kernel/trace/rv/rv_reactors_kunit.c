// SPDX-License-Identifier: GPL-2.0
/*
 * KUnit tests for RV reactor registration and dispatch.
 *
 * The dispatch tests rely on reacting_on being enabled, since rv_react()
 * returns early when it is off. It is on by default when the suites run
 * built-in; as a module, re-enable it if disabled via
 * /sys/kernel/tracing/rv/reacting_on.
 */

#include <kunit/test.h>
#include <linux/build_bug.h>
#include <linux/rv.h>
#include <linux/delay.h>
#include "rv.h"

static struct rv_reactor test_reactor = {
	.name		= "kunit_test_reactor",
	.description	= "KUnit test reactor",
};

static void reactor_teardown(void *arg)
{
	rv_unregister_reactor(&test_reactor);
}

static void register_test_reactor(struct kunit *test)
{
	KUNIT_ASSERT_EQ(test, rv_register_reactor(&test_reactor), 0);
	KUNIT_ASSERT_EQ(test,
			kunit_add_action_or_reset(test, reactor_teardown, NULL), 0);
}

static void test_double_register(struct kunit *test)
{
	register_test_reactor(test);
	KUNIT_EXPECT_EQ(test, rv_register_reactor(&test_reactor), -EINVAL);
}

static const char long_reactor_name[] = "kunit_reactor_name_too_long_xxx_";
static_assert(sizeof(long_reactor_name) - 1 >= MAX_RV_REACTOR_NAME_SIZE,
	       "long_reactor_name must be at least MAX_RV_REACTOR_NAME_SIZE chars");

static void test_name_too_long(struct kunit *test)
{
	static struct rv_reactor long_reactor = {
		.name = long_reactor_name,
	};

	KUNIT_EXPECT_EQ(test, rv_register_reactor(&long_reactor), -EINVAL);
}

static struct kunit_case rv_reactor_registration_cases[] = {
	KUNIT_CASE(test_double_register),
	KUNIT_CASE(test_name_too_long),
	{}
};

static struct kunit_suite rv_reactor_registration_suite = {
	.name		= "rv_reactor_registration",
	.test_cases	= rv_reactor_registration_cases,
};

static int react_call_count;

__printf(1, 0) static void mock_react(const char *msg, va_list args)
{
	react_call_count++;
	/* Busy-wait so a timer interrupt fires inside rv_react(). */
	mdelay(20);
}

static void test_react_no_callback(struct kunit *test)
{
	struct rv_monitor monitor = {
		.name = "kunit_null_react",
	};

	react_call_count = 0;
	rv_react(&monitor, "no callback");

	KUNIT_EXPECT_EQ(test, react_call_count, 0);
}

static void test_react_callback_invoked(struct kunit *test)
{
	struct rv_monitor monitor = {
		.name	= "kunit_dispatch_monitor",
		.react	= mock_react,
	};

	react_call_count = 0;
	rv_react(&monitor, "callback invocation test");
	KUNIT_EXPECT_EQ(test, react_call_count, 1);
}

static struct kunit_case rv_react_dispatch_cases[] = {
	KUNIT_CASE(test_react_no_callback),
	KUNIT_CASE(test_react_callback_invoked),
	{}
};

static struct kunit_suite rv_react_dispatch_suite = {
	.name		= "rv_react_dispatch",
	.test_cases	= rv_react_dispatch_cases,
};

kunit_test_suites(&rv_reactor_registration_suite, &rv_react_dispatch_suite);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("KUnit tests for RV reactor registration and dispatch");
