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

/**
 * struct alarm_base - Alarm timer bases
 * @lock:		Lock for syncrhonized access to the base
 * @timerqueue:		Timerqueue head managing the list of events
 * @timer: 		hrtimer used to schedule events while running
 * @gettime:		Function to read the time correlating to the base
 * @base_clockid:	clockid for the base
 */
static struct alarm_base {
	spinlock_t		lock;
	struct timerqueue_head	timerqueue;
	ktime_t			(*gettime)(void);
	clockid_t		base_clockid;
} alarm_bases[ALARM_NUMTYPE];

/* freezer delta & lock used to handle clock_nanosleep triggered wakeups */
static ktime_t freezer_delta;
static DEFINE_SPINLOCK(freezer_delta_lock);

static struct wakeup_source *ws;

#ifdef CONFIG_RTC_CLASS
/* rtc timer and device for setting alarm wakeups at suspend */
static struct rtc_timer		rtctimer;
static struct rtc_device	*rtcdev;
static DEFINE_SPINLOCK(rtcdev_lock);

/**
 * alarmtimer_get_rtcdev - Return selected rtcdevice
 *
 * This function returns the rtc device to use for wakealarms.
 * If one has not already been chosen, it checks to see if a
 * functional rtc device is available.
 */
struct rtc_device *alarmtimer_get_rtcdev(void)
{
	unsigned long flags;
	struct rtc_device *ret;

	spin_lock_irqsave(&rtcdev_lock, flags);
	ret = rtcdev;
	spin_unlock_irqrestore(&rtcdev_lock, flags);

	return ret;
}


static int alarmtimer_rtc_add_device(struct device *dev,
				struct class_interface *class_intf)
{
	unsigned long flags;
	struct rtc_device *rtc = to_rtc_device(dev);

	if (rtcdev)
		return -EBUSY;

	if (!rtc->ops->set_alarm)
		return -1;
	if (!device_may_wakeup(rtc->dev.parent))
		return -1;

	spin_lock_irqsave(&rtcdev_lock, flags);
	if (!rtcdev) {
		rtcdev = rtc;
		/* hold a reference so it doesn't go away */
		get_device(dev);
	}
	spin_unlock_irqrestore(&rtcdev_lock, flags);
	return 0;
}

static inline void alarmtimer_rtc_timer_init(void)
{
	rtc_timer_init(&rtctimer, NULL, NULL);
}

static struct class_interface alarmtimer_rtc_interface = {
	.add_dev = &alarmtimer_rtc_add_device,
};

static int alarmtimer_rtc_interface_setup(void)
{
	alarmtimer_rtc_interface.class = rtc_class;
	return class_interface_register(&alarmtimer_rtc_interface);
}
static void alarmtimer_rtc_interface_remove(void)
{
	class_interface_unregister(&alarmtimer_rtc_interface);
}
#else
struct rtc_device *alarmtimer_get_rtcdev(void)
{
	return NULL;
}
#define rtcdev (NULL)
static inline int alarmtimer_rtc_interface_setup(void) { return 0; }
static inline void alarmtimer_rtc_interface_remove(void) { }
static inline void alarmtimer_rtc_timer_init(void) { }
#endif

/**
 * alarmtimer_enqueue - Adds an alarm timer to an alarm_base timerqueue
 * @base: pointer to the base where the timer is being run
 * @alarm: pointer to alarm being enqueued.
 *
 * Adds alarm to a alarm_base timerqueue
 *
 * Must hold base->lock when calling.
 */
static void alarmtimer_enqueue(struct alarm_base *base, struct alarm *alarm)
{
	if (alarm->state & ALARMTIMER_STATE_ENQUEUED)
		timerqueue_del(&base->timerqueue, &alarm->node);

	timerqueue_add(&base->timerqueue, &alarm->node);
	alarm->state |= ALARMTIMER_STATE_ENQUEUED;
}

/**
 * alarmtimer_dequeue - Removes an alarm timer from an alarm_base timerqueue
 * @base: pointer to the base where the timer is running
 * @alarm: pointer to alarm being removed
 *
 * Removes alarm to a alarm_base timerqueue
 *
 * Must hold base->lock when calling.
 */
static void alarmtimer_dequeue(struct alarm_base *base, struct alarm *alarm)
{
	if (!(alarm->state & ALARMTIMER_STATE_ENQUEUED))
		return;

	timerqueue_del(&base->timerqueue, &alarm->node);
	alarm->state &= ~ALARMTIMER_STATE_ENQUEUED;
}


/**
 * alarmtimer_fired - Handles alarm hrtimer being fired.
 * @timer: pointer to hrtimer being run
 *
 * When a alarm timer fires, this runs through the timerqueue to
 * see which alarms expired, and runs those. If there are more alarm
 * timers queued for the future, we set the hrtimer to fire when
 * when the next future alarm timer expires.
 */
static enum hrtimer_restart alarmtimer_fired(struct hrtimer *timer)
{
	struct alarm *alarm = container_of(timer, struct alarm, timer);
	struct alarm_base *base = &alarm_bases[alarm->type];
	unsigned long flags;
	int ret = HRTIMER_NORESTART;
	int restart = ALARMTIMER_NORESTART;

	spin_lock_irqsave(&base->lock, flags);
	alarmtimer_dequeue(base, alarm);
	spin_unlock_irqrestore(&base->lock, flags);

	if (alarm->function)
		restart = alarm->function(alarm, base->gettime());

	spin_lock_irqsave(&base->lock, flags);
	if (restart != ALARMTIMER_NORESTART) {
		hrtimer_set_expires(&alarm->timer, alarm->node.expires);
		alarmtimer_enqueue(base, alarm);
		ret = HRTIMER_RESTART;
	}
	spin_unlock_irqrestore(&base->lock, flags);

	return ret;

}

ktime_t alarm_expires_remaining(const struct alarm *alarm)
{
	struct alarm_base *base = &alarm_bases[alarm->type];
	return ktime_sub(alarm->node.expires, base->gettime());
}
EXPORT_SYMBOL_GPL(alarm_expires_remaining);

#ifdef CONFIG_RTC_CLASS
/**
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
	struct rtc_device *rtc;
	int i;
	int ret;

	spin_lock_irqsave(&freezer_delta_lock, flags);
	min = freezer_delta;
	freezer_delta = ktime_set(0, 0);
	spin_unlock_irqrestore(&freezer_delta_lock, flags);

	rtc = alarmtimer_get_rtcdev();
	/* If we have no rtcdev, just return */
	if (!rtc)
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

	if (ktime_to_ns(min) < 2 * NSEC_PER_SEC) {
		__pm_wakeup_event(ws, 2 * MSEC_PER_SEC);
		return -EBUSY;
	}

	/* Setup an rtc timer to fire that far in the future */
	rtc_timer_cancel(rtc, &rtctimer);
	rtc_read_time(rtc, &tm);
	now = rtc_tm_to_ktime(tm);
	now = ktime_add(now, min);

	/* Set alarm, if in the past reject suspend briefly to handle */
	ret = rtc_timer_start(rtc, &rtctimer, now, ktime_set(0, 0));
	if (ret < 0)
		__pm_wakeup_event(ws, MSEC_PER_SEC);
	return ret;
}
#else
static int alarmtimer_suspend(struct device *dev)
{
	return 0;
}
#endif

static void alarmtimer_freezerset(ktime_t absexp, enum alarmtimer_type type)
{
	ktime_t delta;
	unsigned long flags;
	struct alarm_base *base = &alarm_bases[type];

	delta = ktime_sub(absexp, base->gettime());

	spin_lock_irqsave(&freezer_delta_lock, flags);
	if (!freezer_delta.tv64 || (delta.tv64 < freezer_delta.tv64))
		freezer_delta = delta;
	spin_unlock_irqrestore(&freezer_delta_lock, flags);
}


/**
 * alarm_init - Initialize an alarm structure
 * @alarm: ptr to alarm to be initialized
 * @type: the type of the alarm
 * @function: callback that is run when the alarm fires
 */
void alarm_init(struct alarm *alarm, enum alarmtimer_type type,
		enum alarmtimer_restart (*function)(struct alarm *, ktime_t))
{
	timerqueue_init(&alarm->node);
	hrtimer_init(&alarm->timer, alarm_bases[type].base_clockid,
			HRTIMER_MODE_ABS);
	alarm->timer.function = alarmtimer_fired;
	alarm->function = function;
	alarm->type = type;
	alarm->state = ALARMTIMER_STATE_INACTIVE;
}
EXPORT_SYMBOL_GPL(alarm_init);

/**
 * alarm_start - Sets an absolute alarm to fire
 * @alarm: ptr to alarm to set
 * @start: time to run the alarm
 */
int alarm_start(struct alarm *alarm, ktime_t start)
{
	struct alarm_base *base = &alarm_bases[alarm->type];
	unsigned long flags;
	int ret;

	spin_lock_irqsave(&base->lock, flags);
	alarm->node.expires = start;
	alarmtimer_enqueue(base, alarm);
	ret = hrtimer_start(&alarm->timer, alarm->node.expires,
				HRTIMER_MODE_ABS);
	spin_unlock_irqrestore(&base->lock, flags);
	return ret;
}
EXPORT_SYMBOL_GPL(alarm_start);

/**
 * alarm_start_relative - Sets a relative alarm to fire
 * @alarm: ptr to alarm to set
 * @start: time relative to now to run the alarm
 */
int alarm_start_relative(struct alarm *alarm, ktime_t start)
{
	struct alarm_base *base = &alarm_bases[alarm->type];

	start = ktime_add(start, base->gettime());
	return alarm_start(alarm, start);
}
EXPORT_SYMBOL_GPL(alarm_start_relative);

void alarm_restart(struct alarm *alarm)
{
	struct alarm_base *base = &alarm_bases[alarm->type];
	unsigned long flags;

	spin_lock_irqsave(&base->lock, flags);
	hrtimer_set_expires(&alarm->timer, alarm->node.expires);
	hrtimer_restart(&alarm->timer);
	alarmtimer_enqueue(base, alarm);
	spin_unlock_irqrestore(&base->lock, flags);
}
EXPORT_SYMBOL_GPL(alarm_restart);

/**
 * alarm_try_to_cancel - Tries to cancel an alarm timer
 * @alarm: ptr to alarm to be canceled
 *
 * Returns 1 if the timer was canceled, 0 if it was not running,
 * and -1 if the callback was running
 */
int alarm_try_to_cancel(struct alarm *alarm)
{
	struct alarm_base *base = &alarm_bases[alarm->type];
	unsigned long flags;
	int ret;

	spin_lock_irqsave(&base->lock, flags);
	ret = hrtimer_try_to_cancel(&alarm->timer);
	if (ret >= 0)
		alarmtimer_dequeue(base, alarm);
	spin_unlock_irqrestore(&base->lock, flags);
	return ret;
}
EXPORT_SYMBOL_GPL(alarm_try_to_cancel);


/**
 * alarm_cancel - Spins trying to cancel an alarm timer until it is done
 * @alarm: ptr to alarm to be canceled
 *
 * Returns 1 if the timer was canceled, 0 if it was not active.
 */
int alarm_cancel(struct alarm *alarm)
{
	for (;;) {
		int ret = alarm_try_to_cancel(alarm);
		if (ret >= 0)
			return ret;
		cpu_relax();
	}
}
EXPORT_SYMBOL_GPL(alarm_cancel);


u64 alarm_forward(struct alarm *alarm, ktime_t now, ktime_t interval)
{
	u64 overrun = 1;
	ktime_t delta;

	delta = ktime_sub(now, alarm->node.expires);

	if (delta.tv64 < 0)
		return 0;

	if (unlikely(delta.tv64 >= interval.tv64)) {
		s64 incr = ktime_to_ns(interval);

		overrun = ktime_divns(delta, incr);

		alarm->node.expires = ktime_add_ns(alarm->node.expires,
							incr*overrun);

		if (alarm->node.expires.tv64 > now.tv64)
			return overrun;
		/*
		 * This (and the ktime_add() below) is the
		 * correction for exact:
		 */
		overrun++;
	}

	alarm->node.expires = ktime_add(alarm->node.expires, interval);
	return overrun;
}
EXPORT_SYMBOL_GPL(alarm_forward);

u64 alarm_forward_now(struct alarm *alarm, ktime_t interval)
{
	struct alarm_base *base = &alarm_bases[alarm->type];

	return alarm_forward(alarm, base->gettime(), interval);
}
EXPORT_SYMBOL_GPL(alarm_forward_now);


/**
 * clock2alarm - helper that converts from clockid to alarmtypes
 * @clockid: clockid.
 */
static enum alarmtimer_type clock2alarm(clockid_t clockid)
{
	if (clockid == CLOCK_REALTIME_ALARM)
		return ALARM_REALTIME;
	if (clockid == CLOCK_BOOTTIME_ALARM)
		return ALARM_BOOTTIME;
	return -1;
}

/**
 * alarm_handle_timer - Callback for posix timers
 * @alarm: alarm that fired
 *
 * Posix timer callback for expired alarm timers.
 */
static enum alarmtimer_restart alarm_handle_timer(struct alarm *alarm,
							ktime_t now)
{
	struct k_itimer *ptr = container_of(alarm, struct k_itimer,
						it.alarm.alarmtimer);
	if (posix_timer_event(ptr, 0) != 0)
		ptr->it_overrun++;

	/* Re-add periodic timers */
	if (ptr->it.alarm.interval.tv64) {
		ptr->it_overrun += alarm_forward(alarm, now,
						ptr->it.alarm.interval);
		return ALARMTIMER_RESTART;
	}
	return ALARMTIMER_NORESTART;
}

/**
 * alarm_clock_getres - posix getres interface
 * @which_clock: clockid
 * @tp: timespec to fill
 *
 * Returns the granularity of underlying alarm base clock
 */
static int alarm_clock_getres(const clockid_t which_clock, struct timespec *tp)
{
	clockid_t baseid = alarm_bases[clock2alarm(which_clock)].base_clockid;

	if (!alarmtimer_get_rtcdev())
		return -EINVAL;

	return hrtimer_get_res(baseid, tp);
}

/**
 * alarm_clock_get - posix clock_get interface
 * @which_clock: clockid
 * @tp: timespec to fill.
 *
 * Provides the underlying alarm base time.
 */
static int alarm_clock_get(clockid_t which_clock, struct timespec *tp)
{
	struct alarm_base *base = &alarm_bases[clock2alarm(which_clock)];

	if (!alarmtimer_get_rtcdev())
		return -EINVAL;

	*tp = ktime_to_timespec(base->gettime());
	return 0;
}

/**
 * alarm_timer_create - posix timer_create interface
 * @new_timer: k_itimer pointer to manage
 *
 * Initializes the k_itimer structure.
 */
static int alarm_timer_create(struct k_itimer *new_timer)
{
	enum  alarmtimer_type type;
	struct alarm_base *base;

	if (!alarmtimer_get_rtcdev())
		return -ENOTSUPP;

	if (!capable(CAP_WAKE_ALARM))
		return -EPERM;

	type = clock2alarm(new_timer->it_clock);
	base = &alarm_bases[type];
	alarm_init(&new_timer->it.alarm.alarmtimer, type, alarm_handle_timer);
	return 0;
}

/**
 * alarm_timer_get - posix timer_get interface
 * @new_timer: k_itimer pointer
 * @cur_setting: itimerspec data to fill
 *
 * Copies the itimerspec data out from the k_itimer
 */
static void alarm_timer_get(struct k_itimer *timr,
				struct itimerspec *cur_setting)
{
	memset(cur_setting, 0, sizeof(struct itimerspec));

	cur_setting->it_interval =
			ktime_to_timespec(timr->it.alarm.interval);
	cur_setting->it_value =
		ktime_to_timespec(timr->it.alarm.alarmtimer.node.expires);
	return;
}

/**
 * alarm_timer_del - posix timer_del interface
 * @timr: k_itimer pointer to be deleted
 *
 * Cancels any programmed alarms for the given timer.
 */
static int alarm_timer_del(struct k_itimer *timr)
{
	if (!rtcdev)
		return -ENOTSUPP;

	if (alarm_try_to_cancel(&timr->it.alarm.alarmtimer) < 0)
		return TIMER_RETRY;

	return 0;
}

/**
 * alarm_timer_set - posix timer_set interface
 * @timr: k_itimer pointer to be deleted
 * @flags: timer flags
 * @new_setting: itimerspec to be used
 * @old_setting: itimerspec being replaced
 *
 * Sets the timer to new_setting, and starts the timer.
 */
static int alarm_timer_set(struct k_itimer *timr, int flags,
				struct itimerspec *new_setting,
				struct itimerspec *old_setting)
{
	ktime_t exp;

	if (!rtcdev)
		return -ENOTSUPP;

	if (flags & ~TIMER_ABSTIME)
		return -EINVAL;

	if (old_setting)
		alarm_timer_get(timr, old_setting);

	/* If the timer was already set, cancel it */
	if (alarm_try_to_cancel(&timr->it.alarm.alarmtimer) < 0)
		return TIMER_RETRY;

	/* start the timer */
	timr->it.alarm.interval = timespec_to_ktime(new_setting->it_interval);
	exp = timespec_to_ktime(new_setting->it_value);
	/* Convert (if necessary) to absolute time */
	if (flags != TIMER_ABSTIME) {
		ktime_t now;

		now = alarm_bases[timr->it.alarm.alarmtimer.type].gettime();
		exp = ktime_add(now, exp);
	}

	alarm_start(&timr->it.alarm.alarmtimer, exp);
	return 0;
}

/**
 * alarmtimer_nsleep_wakeup - Wakeup function for alarm_timer_nsleep
 * @alarm: ptr to alarm that fired
 *
 * Wakes up the task that set the alarmtimer
 */
static enum alarmtimer_restart alarmtimer_nsleep_wakeup(struct alarm *alarm,
								ktime_t now)
{
	struct task_struct *task = (struct task_struct *)alarm->data;

	alarm->data = NULL;
	if (task)
		wake_up_process(task);
	return ALARMTIMER_NORESTART;
}

/**
 * alarmtimer_do_nsleep - Internal alarmtimer nsleep implementation
 * @alarm: ptr to alarmtimer
 * @absexp: absolute expiration time
 *
 * Sets the alarm timer and sleeps until it is fired or interrupted.
 */
static int alarmtimer_do_nsleep(struct alarm *alarm, ktime_t absexp)
{
	alarm->data = (void *)current;
	do {
		set_current_state(TASK_INTERRUPTIBLE);
		alarm_start(alarm, absexp);
		if (likely(alarm->data))
			schedule();

		alarm_cancel(alarm);
	} while (alarm->data && !signal_pending(current));

	__set_current_state(TASK_RUNNING);

	return (alarm->data == NULL);
}


/**
 * update_rmtp - Update remaining timespec value
 * @exp: expiration time
 * @type: timer type
 * @rmtp: user pointer to remaining timepsec value
 *
 * Helper function that fills in rmtp value with time between
 * now and the exp value
 */
static int update_rmtp(ktime_t exp, enum  alarmtimer_type type,
			struct timespec __user *rmtp)
{
	struct timespec rmt;
	ktime_t rem;

	rem = ktime_sub(exp, alarm_bases[type].gettime());

	if (rem.tv64 <= 0)
		return 0;
	rmt = ktime_to_timespec(rem);

	if (copy_to_user(rmtp, &rmt, sizeof(*rmtp)))
		return -EFAULT;

	return 1;

}

/**
 * alarm_timer_nsleep_restart - restartblock alarmtimer nsleep
 * @restart: ptr to restart block
 *
 * Handles restarted clock_nanosleep calls
 */
static long __sched alarm_timer_nsleep_restart(struct restart_block *restart)
{
	enum  alarmtimer_type type = restart->nanosleep.clockid;
	ktime_t exp;
	struct timespec __user  *rmtp;
	struct alarm alarm;
	int ret = 0;

	exp.tv64 = restart->nanosleep.expires;
	alarm_init(&alarm, type, alarmtimer_nsleep_wakeup);

	if (alarmtimer_do_nsleep(&alarm, exp))
		goto out;

	if (freezing(current))
		alarmtimer_freezerset(exp, type);

	rmtp = restart->nanosleep.rmtp;
	if (rmtp) {
		ret = update_rmtp(exp, type, rmtp);
		if (ret <= 0)
			goto out;
	}


	/* The other values in restart are already filled in */
	ret = -ERESTART_RESTARTBLOCK;
out:
	return ret;
}

/**
 * alarm_timer_nsleep - alarmtimer nanosleep
 * @which_clock: clockid
 * @flags: determins abstime or relative
 * @tsreq: requested sleep time (abs or rel)
 * @rmtp: remaining sleep time saved
 *
 * Handles clock_nanosleep calls against _ALARM clockids
 */
static int alarm_timer_nsleep(const clockid_t which_clock, int flags,
		     struct timespec *tsreq, struct timespec __user *rmtp)
{
	enum  alarmtimer_type type = clock2alarm(which_clock);
	struct alarm alarm;
	ktime_t exp;
	int ret = 0;
	struct restart_block *restart;

	if (!alarmtimer_get_rtcdev())
		return -ENOTSUPP;

	if (flags & ~TIMER_ABSTIME)
		return -EINVAL;

	if (!capable(CAP_WAKE_ALARM))
		return -EPERM;

	alarm_init(&alarm, type, alarmtimer_nsleep_wakeup);

	exp = timespec_to_ktime(*tsreq);
	/* Convert (if necessary) to absolute time */
	if (flags != TIMER_ABSTIME) {
		ktime_t now = alarm_bases[type].gettime();
		exp = ktime_add(now, exp);
	}

	if (alarmtimer_do_nsleep(&alarm, exp))
		goto out;

	if (freezing(current))
		alarmtimer_freezerset(exp, type);

	/* abs timers don't set remaining time or restart */
	if (flags == TIMER_ABSTIME) {
		ret = -ERESTARTNOHAND;
		goto out;
	}

	if (rmtp) {
		ret = update_rmtp(exp, type, rmtp);
		if (ret <= 0)
			goto out;
	}

	restart = &current_thread_info()->restart_block;
	restart->fn = alarm_timer_nsleep_restart;
	restart->nanosleep.clockid = type;
	restart->nanosleep.expires = exp.tv64;
	restart->nanosleep.rmtp = rmtp;
	ret = -ERESTART_RESTARTBLOCK;

out:
	return ret;
}


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
	struct platform_device *pdev;
	int error = 0;
	int i;
	struct k_clock alarm_clock = {
		.clock_getres	= alarm_clock_getres,
		.clock_get	= alarm_clock_get,
		.timer_create	= alarm_timer_create,
		.timer_set	= alarm_timer_set,
		.timer_del	= alarm_timer_del,
		.timer_get	= alarm_timer_get,
		.nsleep		= alarm_timer_nsleep,
	};

	alarmtimer_rtc_timer_init();

	posix_timers_register_clock(CLOCK_REALTIME_ALARM, &alarm_clock);
	posix_timers_register_clock(CLOCK_BOOTTIME_ALARM, &alarm_clock);

	/* Initialize alarm bases */
	alarm_bases[ALARM_REALTIME].base_clockid = CLOCK_REALTIME;
	alarm_bases[ALARM_REALTIME].gettime = &ktime_get_real;
	alarm_bases[ALARM_BOOTTIME].base_clockid = CLOCK_BOOTTIME;
	alarm_bases[ALARM_BOOTTIME].gettime = &ktime_get_boottime;
	for (i = 0; i < ALARM_NUMTYPE; i++) {
		timerqueue_init_head(&alarm_bases[i].timerqueue);
		spin_lock_init(&alarm_bases[i].lock);
	}

	error = alarmtimer_rtc_interface_setup();
	if (error)
		return error;

	error = platform_driver_register(&alarmtimer_driver);
	if (error)
		goto out_if;

	pdev = platform_device_register_simple("alarmtimer", -1, NULL, 0);
	if (IS_ERR(pdev)) {
		error = PTR_ERR(pdev);
		goto out_drv;
	}
	ws = wakeup_source_register("alarmtimer");
	return 0;

out_drv:
	platform_driver_unregister(&alarmtimer_driver);
out_if:
	alarmtimer_rtc_interface_remove();
	return error;
}
device_initcall(alarmtimer_init);
