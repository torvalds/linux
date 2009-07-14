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
#include <linux/tick.h>

#include "tick-internal.h"

/**
 * tick_program_event internal worker function
 */
int tick_dev_program_event(struct clock_event_device *dev, ktime_t expires,
			   int force)
{
	ktime_t now = ktime_get();
	int i;

	for (i = 0;;) {
		int ret = clockevents_program_event(dev, expires, now);

		if (!ret || !force)
			return ret;

		/*
		 * We tried 2 times to program the device with the given
		 * min_delta_ns. If that's not working then we double it
		 * and emit a warning.
		 */
		if (++i > 2) {
			/* Increase the min. delta and try again */
			if (!dev->min_delta_ns)
				dev->min_delta_ns = 5000;
			else
				dev->min_delta_ns += dev->min_delta_ns >> 1;

			printk(KERN_WARNING
			       "CE: %s increasing min_delta_ns to %lu nsec\n",
			       dev->name ? dev->name : "?",
			       dev->min_delta_ns << 1);

			i = 0;
		}

		now = ktime_get();
		expires = ktime_add_ns(now, dev->min_delta_ns);
	}
}

/**
 * tick_program_event
 */
int tick_program_event(ktime_t expires, int force)
{
	struct clock_event_device *dev = __get_cpu_var(tick_cpu_device).evtdev;

	return tick_dev_program_event(dev, expires, force);
}

/**
 * tick_resume_onshot - resume oneshot mode
 */
void tick_resume_oneshot(void)
{
	struct tick_device *td = &__get_cpu_var(tick_cpu_device);
	struct clock_event_device *dev = td->evtdev;

	clockevents_set_mode(dev, CLOCK_EVT_MODE_ONESHOT);
	tick_program_event(ktime_get(), 1);
}

/**
 * tick_setup_oneshot - setup the event device for oneshot mode (hres or nohz)
 */
void tick_setup_oneshot(struct clock_event_device *newdev,
			void (*handler)(struct clock_event_device *),
			ktime_t next_event)
{
	newdev->event_handler = handler;
	clockevents_set_mode(newdev, CLOCK_EVT_MODE_ONESHOT);
	tick_dev_program_event(newdev, next_event, 1);
}

/**
 * tick_switch_to_oneshot - switch to oneshot mode
 */
int tick_switch_to_oneshot(void (*handler)(struct clock_event_device *))
{
	struct tick_device *td = &__get_cpu_var(tick_cpu_device);
	struct clock_event_device *dev = td->evtdev;

	if (!dev || !(dev->features & CLOCK_EVT_FEAT_ONESHOT) ||
		    !tick_device_is_functional(dev)) {

		printk(KERN_INFO "Clockevents: "
		       "could not switch to one-shot mode:");
		if (!dev) {
			printk(" no tick device\n");
		} else {
			if (!tick_device_is_functional(dev))
				printk(" %s is not functional.\n", dev->name);
			else
				printk(" %s does not support one-shot mode.\n",
				       dev->name);
		}
		return -EINVAL;
	}

	td->mode = TICKDEV_MODE_ONESHOT;
	dev->event_handler = handler;
	clockevents_set_mode(dev, CLOCK_EVT_MODE_ONESHOT);
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
	ret = __get_cpu_var(tick_cpu_device).mode == TICKDEV_MODE_ONESHOT;
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
