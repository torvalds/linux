/*
 * linux/kernel/time/tick-oneshot.c
 *
 * This file contains functions which manage high resolution tick
 * related events.
 *
 * Copyright(C) 2005-2006, Thomas Gleixner <tglx@linutronix.de>
 * Copyright(C) 2005-2007, Red Hat, Inc., Ingo Molnar
 * Copyright(C) 2006-2007, Timesys Corp., Thomas Gleixner
 *
 * This code is licenced under the GPL version 2. For details see
 * kernel-base/COPYING.
 */
#include <linux/cpu.h>
#include <linux/err.h>
#include <linux/hrtimer.h>
#include <linux/interrupt.h>
#include <linux/percpu.h>
#include <linux/profile.h>
#include <linux/sched.h>

#include "tick-internal.h"

/**
 * tick_program_event
 */
int tick_program_event(ktime_t expires, int force)
{
	struct clock_event_device *dev = __this_cpu_read(tick_cpu_device.evtdev);

	if (unlikely(expires == KTIME_MAX)) {
		/*
		 * We don't need the clock event device any more, stop it.
		 */
		clockevents_switch_state(dev, CLOCK_EVT_STATE_ONESHOT_STOPPED);
		dev->next_event = KTIME_MAX;
		return 0;
	}

	if (unlikely(clockevent_state_oneshot_stopped(dev))) {
		/*
		 * We need the clock event again, configure it in ONESHOT mode
		 * before using it.
		 */
		clockevents_switch_state(dev, CLOCK_EVT_STATE_ONESHOT);
	}

	return clockevents_program_event(dev, expires, force);
}

/**
 * tick_resume_onshot - resume oneshot mode
 */
void tick_resume_oneshot(void)
{
	struct clock_event_device *dev = __this_cpu_read(tick_cpu_device.evtdev);

	clockevents_switch_state(dev, CLOCK_EVT_STATE_ONESHOT);
	clockevents_program_event(dev, ktime_get(), true);
}

/**
 * tick_setup_oneshot - setup the event device for oneshot mode (hres or nohz)
 */
void tick_setup_oneshot(struct clock_event_device *newdev,
			void (*handler)(struct clock_event_device *),
			ktime_t next_event)
{
	newdev->event_handler = handler;
	clockevents_switch_state(newdev, CLOCK_EVT_STATE_ONESHOT);
	clockevents_program_event(newdev, next_event, true);
}

/**
 * tick_switch_to_oneshot - switch to oneshot mode
 */
int tick_switch_to_oneshot(void (*handler)(struct clock_event_device *))
{
	struct tick_device *td = this_cpu_ptr(&tick_cpu_device);
	struct clock_event_device *dev = td->evtdev;

	if (!dev || !(dev->features & CLOCK_EVT_FEAT_ONESHOT) ||
		    !tick_device_is_functional(dev)) {

		pr_info("Clockevents: could not switch to one-shot mode:");
		if (!dev) {
			pr_cont(" no tick device\n");
		} else {
			if (!tick_device_is_functional(dev))
				pr_cont(" %s is not functional.\n", dev->name);
			else
				pr_cont(" %s does not support one-shot mode.\n",
					dev->name);
		}
		return -EINVAL;
	}

	td->mode = TICKDEV_MODE_ONESHOT;
	dev->event_handler = handler;
	clockevents_switch_state(dev, CLOCK_EVT_STATE_ONESHOT);
	tick_broadcast_switch_to_oneshot();
	return 0;
}

/**
 * tick_check_oneshot_mode - check whether the system is in oneshot mode
 *
 * returns 1 when either nohz or highres are enabled. otherwise 0.
 */
int tick_oneshot_mode_active(void)
{
	unsigned long flags;
	int ret;

	local_irq_save(flags);
	ret = __this_cpu_read(tick_cpu_device.mode) == TICKDEV_MODE_ONESHOT;
	local_irq_restore(flags);

	return ret;
}

#ifdef CONFIG_HIGH_RES_TIMERS
/**
 * tick_init_highres - switch to high resolution mode
 *
 * Called with interrupts disabled.
 */
int tick_init_highres(void)
{
	return tick_switch_to_oneshot(hrtimer_interrupt);
}
#endif
