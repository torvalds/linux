/*
 * Alarmtimer interface
 *
 * This interface provides a timer which is similarto hrtimers,
 * but triggers a RTC alarm if the box is suspend.
 *
 * This interface is influenced by the Android RTC Alarm timer
 * interface.
 *
 * Copyright (C) 2010 IBM Corperation
 *
 * Author: John Stultz <john.stultz@linaro.org>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#include <linux/time.h>
#include <linux/hrtimer.h>
#include <linux/timerqueue.h>
#include <linux/rtc.h>
#include <linux/alarmtimer.h>
#include <linux/mutex.h>
#include <linux/platform_device.h>
#include <linux/posix-timers.h>
#include <linux/workqueue.h>
#include <linux/freezer.h>


static struct alarm_base {
	spinlock_t		lock;
	struct timerqueue_head	timerqueue;
	struct hrtimer		timer;
	ktime_t			(*gettime)(void);
	clockid_t		base_clockid;
	struct work_struct	irqwork;
} alarm_bases[ALARM_NUMTYPE];

static struct rtc_timer		rtctimer;
static struct rtc_device	*rtcdev;

static ktime_t freezer_delta;
static DEFINE_SPINLOCK(freezer_delta_lock);


/**************************************************************************
 * alarmtimer management code
 */

/*
 * alarmtimer_enqueue - Adds an alarm timer to an alarm_base timerqueue
 * @base: pointer to the base where the timer is being run
 * @alarm: pointer to alarm being enqueued.
 *
 * Adds alarm to a alarm_base timerqueue and if necessary sets
 * an hrtimer to run.
 *
 * Must hold base->lock when calling.
 */
static void alarmtimer_enqueue(struct alarm_base *base, struct alarm *alarm)
{
	timerqueue_add(&base->timerqueue, &alarm->node);
	if (&alarm->node == timerqueue_getnext(&base->timerqueue)) {
		hrtimer_try_to_cancel(&base->timer);
		hrtimer_start(&base->timer, alarm->node.expires,
				HRTIMER_MODE_ABS);
	}
}

/*
 * alarmtimer_remove - Removes an alarm timer from an alarm_base timerqueue
 * @base: pointer to the base where the timer is running
 * @alarm: pointer to alarm being removed
 *
 * Removes alarm to a alarm_base timerqueue and if necessary sets
 * a new timer to run.
 *
 * Must hold base->lock when calling.
 */
static void alarmtimer_remove(struct alarm_base *base, struct alarm *alarm)
{
	struct timerqueue_node *next = timerqueue_getnext(&base->timerqueue);

	timerqueue_del(&base->timerqueue, &alarm->node);
	if (next == &alarm->node) {
		hrtimer_try_to_cancel(&base->timer);
		next = timerqueue_getnext(&base->timerqueue);
		if (!next)
			return;
		hrtimer_start(&base->timer, next->expires, HRTIMER_MODE_ABS);
	}
}

/*
 * alarmtimer_do_work - Handles alarm being fired.
 * @work: pointer to workqueue being run
 *
 * When a timer fires, this runs through the timerqueue to see
 * which alarm timers, and run those that expired. If there are
 * more alarm timers queued, we set the hrtimer to fire in the
 * future.
 */
void alarmtimer_do_work(struct work_struct *work)
{
	struct alarm_base *base = container_of(work, struct alarm_base,
						irqwork);
	struct timerqueue_node *next;
	unsigned long flags;
	ktime_t now;

	spin_lock_irqsave(&base->lock, flags);
	now = base->gettime();
	while ((next = timerqueue_getnext(&base->timerqueue))) {
		struct alarm *alarm;
		ktime_t expired = next->expires;

		if (expired.tv64 >= now.tv64)
			break;

		alarm = container_of(next, struct alarm, node);

		timerqueue_del(&base->timerqueue, &alarm->node);
		alarm->enabled = 0;
		/* Re-add periodic timers */
		if (alarm->period.tv64) {
			alarm->node.expires = ktime_add(expired, alarm->period);
			timerqueue_add(&base->timerqueue, &alarm->node);
			alarm->enabled = 1;
		}
		spin_unlock_irqrestore(&base->lock, flags);
		if (alarm->function)
			alarm->function(alarm);
		spin_lock_irqsave(&base->lock, flags);
	}

	if (next) {
		hrtimer_start(&base->timer, next->expires,
				HRTIMER_MODE_ABS);
	}
	spin_unlock_irqrestore(&base->lock, flags);
}


/*
 * alarmtimer_fired - Handles alarm hrtimer being fired.
 * @timer: pointer to hrtimer being run
 *
 * When a timer fires, this schedules the do_work function to
 * be run.
 */
static enum hrtimer_restart alarmtimer_fired(struct hrtimer *timer)
{
	struct alarm_base *base = container_of(timer, struct alarm_base, timer);
	schedule_work(&base->irqwork);
	return HRTIMER_NORESTART;
}


/*
 * alarmtimer_suspend - Suspend time callback
 * @dev: unused
 * @state: unused
 *
 * When we are going into suspend, we look through the bases
 * to see which is the soonest timer to expire. We then
 * set an rtc timer to fire that far into the future, which
 * will wake us from suspend.
 */
static int alarmtimer_suspend(struct device *dev)
{
	struct rtc_time tm;
	ktime_t min, now;
	unsigned long flags;
	int i;

	spin_lock_irqsave(&freezer_delta_lock, flags);
	min = freezer_delta;
	freezer_delta = ktime_set(0, 0);
	spin_unlock_irqrestore(&freezer_delta_lock, flags);

	/* If we have no rtcdev, just return */
	if (!rtcdev)
		return 0;

	/* Find the soonest timer to expire*/
	for (i = 0; i < ALARM_NUMTYPE; i++) {
		struct alarm_base *base = &alarm_bases[i];
		struct timerqueue_node *next;
		ktime_t delta;

		spin_lock_irqsave(&base->lock, flags);
		next = timerqueue_getnext(&base->timerqueue);
		spin_unlock_irqrestore(&base->lock, flags);
		if (!next)
			continue;
		delta = ktime_sub(next->expires, base->gettime());
		if (!min.tv64 || (delta.tv64 < min.tv64))
			min = delta;
	}
	if (min.tv64 == 0)
		return 0;

	/* XXX - Should we enforce a minimum sleep time? */
	WARN_ON(min.tv64 < NSEC_PER_SEC);

	/* Setup an rtc timer to fire that far in the future */
	rtc_timer_cancel(rtcdev, &rtctimer);
	rtc_read_time(rtcdev, &tm);
	now = rtc_tm_to_ktime(tm);
	now = ktime_add(now, min);

	rtc_timer_start(rtcdev, &rtctimer, now, ktime_set(0, 0));

	return 0;
}


/**************************************************************************
 * alarm kernel interface code
 */

/*
 * alarm_init - Initialize an alarm structure
 * @alarm: ptr to alarm to be initialized
 * @type: the type of the alarm
 * @function: callback that is run when the alarm fires
 *
 * In-kernel interface to initializes the alarm structure.
 */
void alarm_init(struct alarm *alarm, enum alarmtimer_type type,
		void (*function)(struct alarm *))
{
	timerqueue_init(&alarm->node);
	alarm->period = ktime_set(0, 0);
	alarm->function = function;
	alarm->type = type;
	alarm->enabled = 0;
}

/*
 * alarm_start - Sets an alarm to fire
 * @alarm: ptr to alarm to set
 * @start: time to run the alarm
 * @period: period at which the alarm will recur
 *
 * In-kernel interface set an alarm timer.
 */
void alarm_start(struct alarm *alarm, ktime_t start, ktime_t period)
{
	struct alarm_base *base = &alarm_bases[alarm->type];
	unsigned long flags;

	spin_lock_irqsave(&base->lock, flags);
	if (alarm->enabled)
		alarmtimer_remove(base, alarm);
	alarm->node.expires = start;
	alarm->period = period;
	alarmtimer_enqueue(base, alarm);
	alarm->enabled = 1;
	spin_unlock_irqrestore(&base->lock, flags);
}

/*
 * alarm_cancel - Tries to cancel an alarm timer
 * @alarm: ptr to alarm to be canceled
 *
 * In-kernel interface to cancel an alarm timer.
 */
void alarm_cancel(struct alarm *alarm)
{
	struct alarm_base *base = &alarm_bases[alarm->type];
	unsigned long flags;

	spin_lock_irqsave(&base->lock, flags);
	if (alarm->enabled)
		alarmtimer_remove(base, alarm);
	alarm->enabled = 0;
	spin_unlock_irqrestore(&base->lock, flags);
}



/**************************************************************************
 * alarmtimer initialization code
 */

/* Suspend hook structures */
static const struct dev_pm_ops alarmtimer_pm_ops = {
	.suspend = alarmtimer_suspend,
};

static struct platform_driver alarmtimer_driver = {
	.driver = {
		.name = "alarmtimer",
		.pm = &alarmtimer_pm_ops,
	}
};

/**
 * alarmtimer_init - Initialize alarm timer code
 *
 * This function initializes the alarm bases and registers
 * the posix clock ids.
 */
static int __init alarmtimer_init(void)
{
	int error = 0;
	int i;

	/* Initialize alarm bases */
	alarm_bases[ALARM_REALTIME].base_clockid = CLOCK_REALTIME;
	alarm_bases[ALARM_REALTIME].gettime = &ktime_get_real;
	alarm_bases[ALARM_BOOTTIME].base_clockid = CLOCK_BOOTTIME;
	alarm_bases[ALARM_BOOTTIME].gettime = &ktime_get_boottime;
	for (i = 0; i < ALARM_NUMTYPE; i++) {
		timerqueue_init_head(&alarm_bases[i].timerqueue);
		spin_lock_init(&alarm_bases[i].lock);
		hrtimer_init(&alarm_bases[i].timer,
				alarm_bases[i].base_clockid,
				HRTIMER_MODE_ABS);
		alarm_bases[i].timer.function = alarmtimer_fired;
		INIT_WORK(&alarm_bases[i].irqwork, alarmtimer_do_work);
	}
	error = platform_driver_register(&alarmtimer_driver);
	platform_device_register_simple("alarmtimer", -1, NULL, 0);

	return error;
}
device_initcall(alarmtimer_init);

/**
 * has_wakealarm - check rtc device has wakealarm ability
 * @dev: current device
 * @name_ptr: name to be returned
 *
 * This helper function checks to see if the rtc device can wake
 * from suspend.
 */
static int __init has_wakealarm(struct device *dev, void *name_ptr)
{
	struct rtc_device *candidate = to_rtc_device(dev);

	if (!candidate->ops->set_alarm)
		return 0;
	if (!device_may_wakeup(candidate->dev.parent))
		return 0;

	*(const char **)name_ptr = dev_name(dev);
	return 1;
}

/**
 * alarmtimer_init_late - Late initializing of alarmtimer code
 *
 * This function locates a rtc device to use for wakealarms.
 * Run as late_initcall to make sure rtc devices have been
 * registered.
 */
static int __init alarmtimer_init_late(void)
{
	char *str;

	/* Find an rtc device and init the rtc_timer */
	class_find_device(rtc_class, NULL, &str, has_wakealarm);
	if (str)
		rtcdev = rtc_class_open(str);
	if (!rtcdev) {
		printk(KERN_WARNING "No RTC device found, ALARM timers will"
			" not wake from suspend");
	}
	rtc_timer_init(&rtctimer, NULL, NULL);

	return 0;
}
late_initcall(alarmtimer_init_late);
