// SPDX-License-Identifier: GPL-2.0+
/*
 * Unit test for the clocksource watchdog.
 *
 * Copyright (C) 2021 Facebook, Inc.
 * Copyright (C) 2026 Intel Corp.
 *
 * Author: Paul E. McKenney <paulmck@kernel.org>
 * Author: Thomas Gleixner <tglx@kernel.org>
 */
#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/clocksource.h>
#include <linux/delay.h>
#include <linux/module.h>
#include <linux/kthread.h>

#include "tick-internal.h"
#include "timekeeping_internal.h"

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Clocksource watchdog unit test");
MODULE_AUTHOR("Paul E. McKenney <paulmck@kernel.org>");
MODULE_AUTHOR("Thomas Gleixner <tglx@kernel.org>");

enum wdtest_states {
	WDTEST_INJECT_NONE,
	WDTEST_INJECT_DELAY,
	WDTEST_INJECT_POSITIVE,
	WDTEST_INJECT_NEGATIVE,
	WDTEST_INJECT_PERCPU	= 0x100,
};

static enum wdtest_states wdtest_state;
static unsigned long wdtest_test_count;
static ktime_t wdtest_last_ts, wdtest_offset;

#define SHIFT_4000PPM	8

static ktime_t wdtest_get_offset(struct clocksource *cs)
{
	if (wdtest_state < WDTEST_INJECT_PERCPU)
		return wdtest_test_count & 0x1 ? 0 : wdtest_offset >> SHIFT_4000PPM;

	/* Only affect the readout of the "remote" CPU */
	return cs->wd_cpu == smp_processor_id() ? 0 : NSEC_PER_MSEC;
}

static u64 wdtest_ktime_read(struct clocksource *cs)
{
	ktime_t now = ktime_get_raw_fast_ns();
	ktime_t intv = now - wdtest_last_ts;

	/*
	 * Only increment the test counter once per watchdog interval and
	 * store the interval for the offset calculation of this step. This
	 * guarantees a consistent behaviour even if the other side needs
	 * to repeat due to a watchdog read timeout.
	 */
	if (intv > (NSEC_PER_SEC / 4)) {
		WRITE_ONCE(wdtest_test_count, wdtest_test_count + 1);
		wdtest_last_ts = now;
		wdtest_offset = intv;
	}

	switch (wdtest_state & ~WDTEST_INJECT_PERCPU) {
	case WDTEST_INJECT_POSITIVE:
		return now + wdtest_get_offset(cs);
	case WDTEST_INJECT_NEGATIVE:
		return now - wdtest_get_offset(cs);
	case WDTEST_INJECT_DELAY:
		udelay(500);
		return now;
	default:
		return now;
	}
}

#define KTIME_FLAGS (CLOCK_SOURCE_IS_CONTINUOUS |	\
		     CLOCK_SOURCE_CALIBRATED |		\
		     CLOCK_SOURCE_MUST_VERIFY |		\
		     CLOCK_SOURCE_WDTEST)

static struct clocksource clocksource_wdtest_ktime = {
	.name			= "wdtest-ktime",
	.rating			= 10,
	.read			= wdtest_ktime_read,
	.mask			= CLOCKSOURCE_MASK(64),
	.flags			= KTIME_FLAGS,
	.list			= LIST_HEAD_INIT(clocksource_wdtest_ktime.list),
};

static void wdtest_clocksource_reset(enum wdtest_states which, bool percpu)
{
	clocksource_unregister(&clocksource_wdtest_ktime);

	pr_info("Test: State %d percpu %d\n", which, percpu);

	wdtest_state = which;
	if (percpu)
		wdtest_state |= WDTEST_INJECT_PERCPU;
	wdtest_test_count = 0;
	wdtest_last_ts = 0;

	clocksource_wdtest_ktime.rating = 10;
	clocksource_wdtest_ktime.flags = KTIME_FLAGS;
	if (percpu)
		clocksource_wdtest_ktime.flags |= CLOCK_SOURCE_WDTEST_PERCPU;
	clocksource_register_khz(&clocksource_wdtest_ktime, 1000 * 1000);
}

static bool wdtest_execute(enum wdtest_states which, bool percpu, unsigned int expect,
			   unsigned long calls)
{
	wdtest_clocksource_reset(which, percpu);

	for (; READ_ONCE(wdtest_test_count) < calls; msleep(100)) {
		unsigned int flags = READ_ONCE(clocksource_wdtest_ktime.flags);

		if (kthread_should_stop())
			return false;

		if (flags & CLOCK_SOURCE_UNSTABLE) {
			if (expect & CLOCK_SOURCE_UNSTABLE)
				return true;
			pr_warn("Fail: Unexpected unstable\n");
			return false;
		}
		if (flags & CLOCK_SOURCE_VALID_FOR_HRES) {
			if (expect & CLOCK_SOURCE_VALID_FOR_HRES)
				return true;
			pr_warn("Fail: Unexpected valid for highres\n");
			return false;
		}
	}

	if (!expect)
		return true;

	pr_warn("Fail: Timed out\n");
	return false;
}

static bool wdtest_run(bool percpu)
{
	if (!wdtest_execute(WDTEST_INJECT_NONE, percpu, CLOCK_SOURCE_VALID_FOR_HRES, 8))
		return false;

	if (!wdtest_execute(WDTEST_INJECT_DELAY, percpu, 0, 4))
		return false;

	if (!wdtest_execute(WDTEST_INJECT_POSITIVE, percpu, CLOCK_SOURCE_UNSTABLE, 8))
		return false;

	if (!wdtest_execute(WDTEST_INJECT_NEGATIVE, percpu, CLOCK_SOURCE_UNSTABLE, 8))
		return false;

	return true;
}

static int wdtest_func(void *arg)
{
	clocksource_register_khz(&clocksource_wdtest_ktime, 1000 * 1000);
	if (wdtest_run(false)) {
		if (wdtest_run(true))
			pr_info("Success: All tests passed\n");
	}
	clocksource_unregister(&clocksource_wdtest_ktime);

	if (!IS_MODULE(CONFIG_TEST_CLOCKSOURCE_WATCHDOG))
		return 0;

	while (!kthread_should_stop())
		schedule_timeout_interruptible(3600 * HZ);
	return 0;
}

static struct task_struct *wdtest_thread;

static int __init clocksource_wdtest_init(void)
{
	struct task_struct *t = kthread_run(wdtest_func, NULL, "wdtest");

	if (IS_ERR(t)) {
		pr_warn("Failed to create wdtest kthread.\n");
		return PTR_ERR(t);
	}
	wdtest_thread = t;
	return 0;
}
module_init(clocksource_wdtest_init);

static void clocksource_wdtest_cleanup(void)
{
	if (wdtest_thread)
		kthread_stop(wdtest_thread);
}
module_exit(clocksource_wdtest_cleanup);
