// SPDX-License-Identifier: GPL-2.0-only

#include <kunit/test.h>

#include <linux/ratelimit.h>
#include <linux/module.h>

/* a simple boot-time regression test */

#define TESTRL_INTERVAL (5 * HZ)
static DEFINE_RATELIMIT_STATE(testrl, TESTRL_INTERVAL, 3);

#define test_ratelimited(test, expected) \
	KUNIT_ASSERT_EQ(test, ___ratelimit(&testrl, "test_ratelimit_smoke"), (expected))

static void test_ratelimit_smoke(struct kunit *test)
{
	// Check settings.
	KUNIT_ASSERT_GE(test, TESTRL_INTERVAL, 100);

	// Test normal operation.
	test_ratelimited(test, true);
	test_ratelimited(test, true);
	test_ratelimited(test, true);
	test_ratelimited(test, false);

	schedule_timeout_idle(TESTRL_INTERVAL - 40);
	test_ratelimited(test, false);

	schedule_timeout_idle(50);
	test_ratelimited(test, true);

	schedule_timeout_idle(2 * TESTRL_INTERVAL);
	test_ratelimited(test, true);
	test_ratelimited(test, true);

	schedule_timeout_idle(TESTRL_INTERVAL - 40);
	test_ratelimited(test, true);
	schedule_timeout_idle(50);
	test_ratelimited(test, true);
	test_ratelimited(test, true);
	test_ratelimited(test, true);
	test_ratelimited(test, false);

	// Test disabling.
	testrl.burst = 0;
	test_ratelimited(test, false);
	testrl.burst = 2;
	testrl.interval = 0;
	test_ratelimited(test, true);
	test_ratelimited(test, true);
	test_ratelimited(test, true);
	test_ratelimited(test, true);
	test_ratelimited(test, true);
	test_ratelimited(test, true);
	test_ratelimited(test, true);

	// Testing re-enabling.
	testrl.interval = TESTRL_INTERVAL;
	test_ratelimited(test, true);
	test_ratelimited(test, true);
	test_ratelimited(test, false);
	test_ratelimited(test, false);
}

static struct kunit_case sort_test_cases[] = {
	KUNIT_CASE_SLOW(test_ratelimit_smoke),
	{}
};

static struct kunit_suite ratelimit_test_suite = {
	.name = "lib_ratelimit",
	.test_cases = sort_test_cases,
};

kunit_test_suites(&ratelimit_test_suite);

MODULE_DESCRIPTION("___ratelimit() KUnit test suite");
MODULE_LICENSE("GPL");
