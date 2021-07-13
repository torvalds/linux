// SPDX-License-Identifier: GPL-2.0+
/*
 * Unit test for the clocksource watchdog.
 *
 * Copyright (C) 2021 Facebook, Inc.
 *
 * Author: Paul E. McKenney <paulmck@kernel.org>
 */
#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/device.h>
#include <linux/clocksource.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/sched.h> /* for spin_unlock_irq() using preempt_count() m68k */
#include <linux/tick.h>
#include <linux/kthread.h>
#include <linux/delay.h>
#include <linux/prandom.h>
#include <linux/cpu.h>

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Paul E. McKenney <paulmck@kernel.org>");

static int holdoff = IS_BUILTIN(CONFIG_TEST_CLOCKSOURCE_WATCHDOG) ? 10 : 0;
module_param(holdoff, int, 0444);
MODULE_PARM_DESC(holdoff, "Time to wait to start test (s).");

/* Watchdog kthread's task_struct pointer for debug purposes. */
static struct task_struct *wdtest_task;

static u64 wdtest_jiffies_read(struct clocksource *cs)
{
	return (u64)jiffies;
}

/* Assume HZ > 100. */
#define JIFFIES_SHIFT	8

static struct clocksource clocksource_wdtest_jiffies = {
	.name			= "wdtest-jiffies",
	.rating			= 1, /* lowest valid rating*/
	.uncertainty_margin	= TICK_NSEC,
	.read			= wdtest_jiffies_read,
	.mask			= CLOCKSOURCE_MASK(32),
	.flags			= CLOCK_SOURCE_MUST_VERIFY,
	.mult			= TICK_NSEC << JIFFIES_SHIFT, /* details above */
	.shift			= JIFFIES_SHIFT,
	.max_cycles		= 10,
};

static int wdtest_ktime_read_ndelays;
static bool wdtest_ktime_read_fuzz;

static u64 wdtest_ktime_read(struct clocksource *cs)
{
	int wkrn = READ_ONCE(wdtest_ktime_read_ndelays);
	static int sign = 1;
	u64 ret;

	if (wkrn) {
		udelay(cs->uncertainty_margin / 250);
		WRITE_ONCE(wdtest_ktime_read_ndelays, wkrn - 1);
	}
	ret = ktime_get_real_fast_ns();
	if (READ_ONCE(wdtest_ktime_read_fuzz)) {
		sign = -sign;
		ret = ret + sign * 100 * NSEC_PER_MSEC;
	}
	return ret;
}

static void wdtest_ktime_cs_mark_unstable(struct clocksource *cs)
{
	pr_info("--- Marking %s unstable due to clocksource watchdog.\n", cs->name);
}

#define KTIME_FLAGS (CLOCK_SOURCE_IS_CONTINUOUS | \
		     CLOCK_SOURCE_VALID_FOR_HRES | \
		     CLOCK_SOURCE_MUST_VERIFY | \
		     CLOCK_SOURCE_VERIFY_PERCPU)

static struct clocksource clocksource_wdtest_ktime = {
	.name			= "wdtest-ktime",
	.rating			= 300,
	.read			= wdtest_ktime_read,
	.mask			= CLOCKSOURCE_MASK(64),
	.flags			= KTIME_FLAGS,
	.mark_unstable		= wdtest_ktime_cs_mark_unstable,
	.list			= LIST_HEAD_INIT(clocksource_wdtest_ktime.list),
};

/* Reset the clocksource if needed. */
static void wdtest_ktime_clocksource_reset(void)
{
	if (clocksource_wdtest_ktime.flags & CLOCK_SOURCE_UNSTABLE) {
		clocksource_unregister(&clocksource_wdtest_ktime);
		clocksource_wdtest_ktime.flags = KTIME_FLAGS;
		schedule_timeout_uninterruptible(HZ / 10);
		clocksource_register_khz(&clocksource_wdtest_ktime, 1000 * 1000);
	}
}

/* Run the specified series of watchdog tests. */
static int wdtest_func(void *arg)
{
	unsigned long j1, j2;
	char *s;
	int i;

	schedule_timeout_uninterruptible(holdoff * HZ);

	/*
	 * Verify that jiffies-like clocksources get the manually
	 * specified uncertainty margin.
	 */
	pr_info("--- Verify jiffies-like uncertainty margin.\n");
	__clocksource_register(&clocksource_wdtest_jiffies);
	WARN_ON_ONCE(clocksource_wdtest_jiffies.uncertainty_margin != TICK_NSEC);

	j1 = clocksource_wdtest_jiffies.read(&clocksource_wdtest_jiffies);
	schedule_timeout_uninterruptible(HZ);
	j2 = clocksource_wdtest_jiffies.read(&clocksource_wdtest_jiffies);
	WARN_ON_ONCE(j1 == j2);

	clocksource_unregister(&clocksource_wdtest_jiffies);

	/*
	 * Verify that tsc-like clocksources are assigned a reasonable
	 * uncertainty margin.
	 */
	pr_info("--- Verify tsc-like uncertainty margin.\n");
	clocksource_register_khz(&clocksource_wdtest_ktime, 1000 * 1000);
	WARN_ON_ONCE(clocksource_wdtest_ktime.uncertainty_margin < NSEC_PER_USEC);

	j1 = clocksource_wdtest_ktime.read(&clocksource_wdtest_ktime);
	udelay(1);
	j2 = clocksource_wdtest_ktime.read(&clocksource_wdtest_ktime);
	pr_info("--- tsc-like times: %lu - %lu = %lu.\n", j2, j1, j2 - j1);
	WARN_ON_ONCE(time_before(j2, j1 + NSEC_PER_USEC));

	/* Verify tsc-like stability with various numbers of errors injected. */
	for (i = 0; i <= max_cswd_read_retries + 1; i++) {
		if (i <= 1 && i < max_cswd_read_retries)
			s = "";
		else if (i <= max_cswd_read_retries)
			s = ", expect message";
		else
			s = ", expect clock skew";
		pr_info("--- Watchdog with %dx error injection, %lu retries%s.\n", i, max_cswd_read_retries, s);
		WRITE_ONCE(wdtest_ktime_read_ndelays, i);
		schedule_timeout_uninterruptible(2 * HZ);
		WARN_ON_ONCE(READ_ONCE(wdtest_ktime_read_ndelays));
		WARN_ON_ONCE((i <= max_cswd_read_retries) !=
			     !(clocksource_wdtest_ktime.flags & CLOCK_SOURCE_UNSTABLE));
		wdtest_ktime_clocksource_reset();
	}

	/* Verify tsc-like stability with clock-value-fuzz error injection. */
	pr_info("--- Watchdog clock-value-fuzz error injection, expect clock skew and per-CPU mismatches.\n");
	WRITE_ONCE(wdtest_ktime_read_fuzz, true);
	schedule_timeout_uninterruptible(2 * HZ);
	WARN_ON_ONCE(!(clocksource_wdtest_ktime.flags & CLOCK_SOURCE_UNSTABLE));
	clocksource_verify_percpu(&clocksource_wdtest_ktime);
	WRITE_ONCE(wdtest_ktime_read_fuzz, false);

	clocksource_unregister(&clocksource_wdtest_ktime);

	pr_info("--- Done with test.\n");
	return 0;
}

static void wdtest_print_module_parms(void)
{
	pr_alert("--- holdoff=%d\n", holdoff);
}

/* Cleanup function. */
static void clocksource_wdtest_cleanup(void)
{
}

static int __init clocksource_wdtest_init(void)
{
	int ret = 0;

	wdtest_print_module_parms();

	/* Create watchdog-test task. */
	wdtest_task = kthread_run(wdtest_func, NULL, "wdtest");
	if (IS_ERR(wdtest_task)) {
		ret = PTR_ERR(wdtest_task);
		pr_warn("%s: Failed to create wdtest kthread.\n", __func__);
		wdtest_task = NULL;
		return ret;
	}

	return 0;
}

module_init(clocksource_wdtest_init);
module_exit(clocksource_wdtest_cleanup);
