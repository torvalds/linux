// SPDX-License-Identifier: GPL-2.0-only

#include <kunit/test.h>

#include <linux/ratelimit.h>
#include <linux/module.h>
#include <linux/kthread.h>
#include <linux/cpumask.h>

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

	schedule_timeout_idle(TESTRL_INTERVAL / 2);
	test_ratelimited(test, false);

	schedule_timeout_idle(TESTRL_INTERVAL * 3 / 4);
	test_ratelimited(test, true);

	schedule_timeout_idle(2 * TESTRL_INTERVAL);
	test_ratelimited(test, true);
	test_ratelimited(test, true);

	schedule_timeout_idle(TESTRL_INTERVAL / 2 );
	test_ratelimited(test, true);
	schedule_timeout_idle(TESTRL_INTERVAL * 3 / 4);
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

static struct ratelimit_state stressrl = RATELIMIT_STATE_INIT_FLAGS("stressrl", HZ / 10, 3,
								    RATELIMIT_MSG_ON_RELEASE);

static int doneflag;
static const int stress_duration = 2 * HZ;

struct stress_kthread {
	unsigned long nattempts;
	unsigned long nunlimited;
	unsigned long nlimited;
	unsigned long nmissed;
	struct task_struct *tp;
};

static int test_ratelimit_stress_child(void *arg)
{
	struct stress_kthread *sktp = arg;

	set_user_nice(current, MAX_NICE);
	WARN_ON_ONCE(!sktp->tp);

	while (!READ_ONCE(doneflag)) {
		sktp->nattempts++;
		if (___ratelimit(&stressrl, __func__))
			sktp->nunlimited++;
		else
			sktp->nlimited++;
		cond_resched();
	}

	sktp->nmissed = ratelimit_state_reset_miss(&stressrl);
	return 0;
}

static void test_ratelimit_stress(struct kunit *test)
{
	int i;
	const int n_stress_kthread = cpumask_weight(cpu_online_mask);
	struct stress_kthread skt = { 0 };
	struct stress_kthread *sktp = kcalloc(n_stress_kthread, sizeof(*sktp), GFP_KERNEL);

	KUNIT_EXPECT_NOT_NULL_MSG(test, sktp, "Memory allocation failure");
	for (i = 0; i < n_stress_kthread; i++) {
		sktp[i].tp = kthread_run(test_ratelimit_stress_child, &sktp[i], "%s/%i",
					 "test_ratelimit_stress_child", i);
		KUNIT_EXPECT_NOT_NULL_MSG(test, sktp, "kthread creation failure");
		pr_alert("Spawned test_ratelimit_stress_child %d\n", i);
	}
	schedule_timeout_idle(stress_duration);
	WRITE_ONCE(doneflag, 1);
	for (i = 0; i < n_stress_kthread; i++) {
		kthread_stop(sktp[i].tp);
		skt.nattempts += sktp[i].nattempts;
		skt.nunlimited += sktp[i].nunlimited;
		skt.nlimited += sktp[i].nlimited;
		skt.nmissed += sktp[i].nmissed;
	}
	KUNIT_ASSERT_EQ_MSG(test, skt.nunlimited + skt.nlimited, skt.nattempts,
			    "Outcomes not equal to attempts");
	KUNIT_ASSERT_EQ_MSG(test, skt.nlimited, skt.nmissed, "Misses not equal to limits");
}

static struct kunit_case ratelimit_test_cases[] = {
	KUNIT_CASE_SLOW(test_ratelimit_smoke),
	KUNIT_CASE_SLOW(test_ratelimit_stress),
	{}
};

static struct kunit_suite ratelimit_test_suite = {
	.name = "lib_ratelimit",
	.test_cases = ratelimit_test_cases,
};

kunit_test_suites(&ratelimit_test_suite);

MODULE_DESCRIPTION("___ratelimit() KUnit test suite");
MODULE_LICENSE("GPL");
