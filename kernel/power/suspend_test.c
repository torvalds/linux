/*
 * kernel/power/suspend_test.c - Suspend to RAM and standby test facility.
 *
 * Copyright (c) 2009 Pavel Machek <pavel@ucw.cz>
 *
 * This file is released under the GPLv2.
 */

#include <linux/init.h>
#include <linux/rtc.h>

#include "power.h"

/*
 * We test the system suspend code by setting an RTC wakealarm a short
 * time in the future, then suspending.  Suspending the devices won't
 * normally take long ... some systems only need a few milliseconds.
 *
 * The time it takes is system-specific though, so when we test this
 * during system bootup we allow a LOT of time.
 */
#define TEST_SUSPEND_SECONDS	10

static unsigned long suspend_test_start_time;

void suspend_test_start(void)
{
	/* FIXME Use better timebase than "jiffies", ideally a clocksource.
	 * What we want is a hardware counter that will work correctly even
	 * during the irqs-are-off stages of the suspend/resume cycle...
	 */
	suspend_test_start_time = jiffies;
}

void suspend_test_finish(const char *label)
{
	long nj = jiffies - suspend_test_start_time;
	unsigned msec;

	msec = jiffies_to_msecs(abs(nj));
	pr_info("PM: %s took %d.%03d seconds\n", label,
			msec / 1000, msec % 1000);

	/* Warning on suspend means the RTC alarm period needs to be
	 * larger -- the system was sooo slooowwww to suspend that the
	 * alarm (should have) fired before the system went to sleep!
	 *
	 * Warning on either suspend or resume also means the system
	 * has some performance issues.  The stack dump of a WARN_ON
	 * is more likely to get the right attention than a printk...
	 */
	WARN(msec > (TEST_SUSPEND_SECONDS * 1000),
	     "Component: %s, time: %u\n", label, msec);
}

/*
 * To test system suspend, we need a hands-off mechanism to resume the
 * system.  RTCs wake alarms are a common self-contained mechanism.
 */

static void __init test_wakealarm(struct rtc_device *rtc, suspend_state_t state)
{
	static char err_readtime[] __initdata =
		KERN_ERR "PM: can't read %s time, err %d\n";
	static char err_wakealarm [] __initdata =
		KERN_ERR "PM: can't set %s wakealarm, err %d\n";
	static char err_suspend[] __initdata =
		KERN_ERR "PM: suspend test failed, error %d\n";
	static char info_test[] __initdata =
		KERN_INFO "PM: test RTC wakeup from '%s' suspend\n";

	unsigned long		now;
	struct rtc_wkalrm	alm;
	int			status;

	/* this may fail if the RTC hasn't been initialized */
	status = rtc_read_time(rtc, &alm.time);
	if (status < 0) {
		printk(err_readtime, dev_name(&rtc->dev), status);
		return;
	}
	rtc_tm_to_time(&alm.time, &now);

	memset(&alm, 0, sizeof alm);
	rtc_time_to_tm(now + TEST_SUSPEND_SECONDS, &alm.time);
	alm.enabled = true;

	status = rtc_set_alarm(rtc, &alm);
	if (status < 0) {
		printk(err_wakealarm, dev_name(&rtc->dev), status);
		return;
	}

	if (state == PM_SUSPEND_MEM) {
		printk(info_test, pm_states[state].label);
		status = pm_suspend(state);
		if (status == -ENODEV)
			state = PM_SUSPEND_STANDBY;
	}
	if (state == PM_SUSPEND_STANDBY) {
		printk(info_test, pm_states[state].label);
		status = pm_suspend(state);
	}
	if (status < 0)
		printk(err_suspend, status);

	/* Some platforms can't detect that the alarm triggered the
	 * wakeup, or (accordingly) disable it after it afterwards.
	 * It's supposed to give oneshot behavior; cope.
	 */
	alm.enabled = false;
	rtc_set_alarm(rtc, &alm);
}

static int __init has_wakealarm(struct device *dev, const void *data)
{
	struct rtc_device *candidate = to_rtc_device(dev);

	if (!candidate->ops->set_alarm)
		return 0;
	if (!device_may_wakeup(candidate->dev.parent))
		return 0;

	return 1;
}

/*
 * Kernel options like "test_suspend=mem" force suspend/resume sanity tests
 * at startup time.  They're normally disabled, for faster boot and because
 * we can't know which states really work on this particular system.
 */
static suspend_state_t test_state __initdata = PM_SUSPEND_ON;

static char warn_bad_state[] __initdata =
	KERN_WARNING "PM: can't test '%s' suspend state\n";

static int __init setup_test_suspend(char *value)
{
	suspend_state_t i;

	/* "=mem" ==> "mem" */
	value++;
	for (i = PM_SUSPEND_MIN; i < PM_SUSPEND_MAX; i++)
		if (!strcmp(pm_states[i].label, value)) {
			test_state = pm_states[i].state;
			return 0;
		}

	printk(warn_bad_state, value);
	return 0;
}
__setup("test_suspend", setup_test_suspend);

static int __init test_suspend(void)
{
	static char		warn_no_rtc[] __initdata =
		KERN_WARNING "PM: no wakealarm-capable RTC driver is ready\n";

	struct rtc_device	*rtc = NULL;
	struct device		*dev;

	/* PM is initialized by now; is that state testable? */
	if (test_state == PM_SUSPEND_ON)
		goto done;
	if (!valid_state(test_state)) {
		printk(warn_bad_state, pm_states[test_state].label);
		goto done;
	}

	/* RTCs have initialized by now too ... can we use one? */
	dev = class_find_device(rtc_class, NULL, NULL, has_wakealarm);
	if (dev)
		rtc = rtc_class_open(dev_name(dev));
	if (!rtc) {
		printk(warn_no_rtc);
		goto done;
	}

	/* go for it */
	test_wakealarm(rtc, test_state);
	rtc_class_close(rtc);
done:
	return 0;
}
late_initcall(test_suspend);
