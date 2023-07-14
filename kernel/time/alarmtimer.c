// SPDX-License-Identifier: GPL-2.0
/*
 * Alarmtimer interface
 *
 * This interface provides a timer which is similar to hrtimers,
 * but triggers a RTC alarm if the box is suspend.
 *
 * This interface is influenced by the Android RTC Alarm timer
 * interface.
 *
 * Copyright (C) 2010 IBM Corporation
 *
 * Author: John Stultz <john.stultz@linaro.org>
 */
#include <linux/time.h>
#include <linux/hrtimer.h>
#include <linux/timerqueue.h>
#include <linux/rtc.h>
#include <linux/sched/signal.h>
#include <linux/sched/debug.h>
#include <linux/alarmtimer.h>
#include <linux/mutex.h>
#include <linux/platform_device.h>
#include <linux/posix-timers.h>
#include <linux/workqueue.h>
#include <linux/freezer.h>
#include <linux/compat.h>
#include <linux/module.h>
#include <linux/time_namespace.h>

#include "posix-timers.h"

#define CREATE_TRACE_POINTS
#include <trace/events/alarmtimer.h>

/**
 * struct alarm_base - Alarm timer bases
 * @lock:		Lock for syncrhonized access to the base
 * @timerqueue:		Timerqueue head managing the list of events
 * @get_ktime:		Function to read the time correlating to the base
 * @get_timespec:	Function to read the namespace time correlating to the base
 * @base_clockid:	clockid for the base
 */
static struct alarm_base {
	spinlock_t		lock;
	struct timerqueue_head	timerqueue;
	ktime_t			(*get_ktime)(void);
	void			(*get_timespec)(struct timespec64 *tp);
	clockid_t		base_clockid;
} alarm_bases[ALARM_NUMTYPE];

#if defined(CONFIG_POSIX_TIMERS) || defined(CONFIG_RTC_CLASS)
/* freezer information to handle clock_nanosleep triggered wakeups */
static enum alarmtimer_type freezer_alarmtype;
static ktime_t freezer_expires;
static ktime_t freezer_delta;
static DEFINE_SPINLOCK(freezer_delta_lock);
#endif

#ifdef CONFIG_RTC_CLASS
/* rtc timer and device for setting alarm wakeups at suspend */
static struct rtc_timer		rtctimer;
static struct rtc_device	*rtcdev;
static DEFINE_SPINLOCK(rtcdev_lock);

/**
 * alarmtimer_get_rtcdev - Return selected rtcdevice
 *
 * This function returns the rtc device to use for wakealarms.
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
EXPORT_SYMBOL_GPL(alarmtimer_get_rtcdev);

static int alarmtimer_rtc_add_device(struct device *dev)
{
	unsigned long flags;
	struct rtc_device *rtc = to_rtc_device(dev);
	struct platform_device *pdev;
	int ret = 0;

	if (rtcdev)
		return -EBUSY;

	if (!test_bit(RTC_FEATURE_ALARM, rtc->features))
		return -1;
	if (!device_may_wakeup(rtc->dev.parent))
		return -1;

	pdev = platform_device_register_data(dev, "alarmtimer",
					     PLATFORM_DEVID_AUTO, NULL, 0);
	if (!IS_ERR(pdev))
		device_init_wakeup(&pdev->dev, true);

	spin_lock_irqsave(&rtcdev_lock, flags);
	if (!IS_ERR(pdev) && !rtcdev) {
		if (!try_module_get(rtc->owner)) {
			ret = -1;
			goto unlock;
		}

		rtcdev = rtc;
		/* hold a reference so it doesn't go away */
		get_device(dev);
		pdev = NULL;
	} else {
		ret = -1;
	}
unlock:
	spin_unlock_irqrestore(&rtcdev_lock, flags);

	platform_device_unregister(pdev);

	return ret;
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
 * the next future alarm timer expires.
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
		restart = alarm->function(alarm, base->get_ktime());

	spin_lock_irqsave(&base->lock, flags);
	if (restart != ALARMTIMER_NORESTART) {
		hrtimer_set_expires(&alarm->timer, alarm->node.expires);
		alarmtimer_enqueue(base, alarm);
		ret = HRTIMER_RESTART;
	}
	spin_unlock_irqrestore(&base->lock, flags);

	trace_alarmtimer_fired(alarm, base->get_ktime());
	return ret;

}

ktime_t alarm_expires_remaining(const struct alarm *alarm)
{
	struct alarm_base *base = &alarm_bases[alarm->type];
	return ktime_sub(alarm->node.expires, base->get_ktime());
}
EXPORT_SYMBOL_GPL(alarm_expires_remaining);

#ifdef CONFIG_RTC_CLASS
/**
 * alarmtimer_suspend - Suspend time callback
 * @dev: unused
 *
 * When we are going into suspend, we look through the bases
 * to see which is the soonest timer to expire. We then
 * set an rtc timer to fire that far into the future, which
 * will wake us from suspend.
 */
static int alarmtimer_suspend(struct device *dev)
{
	ktime_t min, now, expires;
	int i, ret, type;
	struct rtc_device *rtc;
	unsigned long flags;
	struct rtc_time tm;

	spin_lock_irqsave(&freezer_delta_lock, flags);
	min = freezer_delta;
	expires = freezer_expires;
	type = freezer_alarmtype;
	freezer_delta = 0;
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
		delta = ktime_sub(next->expires, base->get_ktime());
		if (!min || (delta < min)) {
			expires = next->expires;
			min = delta;
			type = i;
		}
	}
	if (min == 0)
		return 0;

	if (ktime_to_ns(min) < 2 * NSEC_PER_SEC) {
		pm_wakeup_event(dev, 2 * MSEC_PER_SEC);
		return -EBUSY;
	}

	trace_alarmtimer_suspend(expires, type);

	/* Setup an rtc timer to fire that far in the future */
	rtc_timer_cancel(rtc, &rtctimer);
	rtc_read_time(rtc, &tm);
	now = rtc_tm_to_ktime(tm);
	now = ktime_add(now, min);

	/* Set alarm, if in the past reject suspend briefly to handle */
	ret = rtc_timer_start(rtc, &rtctimer, now, 0);
	if (ret < 0)
		pm_wakeup_event(dev, MSEC_PER_SEC);
	return ret;
}

static int alarmtimer_resume(struct device *dev)
{
	struct rtc_device *rtc;

	rtc = alarmtimer_get_rtcdev();
	if (rtc)
		rtc_timer_cancel(rtc, &rtctimer);
	return 0;
}

#else
static int alarmtimer_suspend(struct device *dev)
{
	return 0;
}

static int alarmtimer_resume(struct device *dev)
{
	return 0;
}
#endif

static void
__alarm_init(struct alarm *alarm, enum alarmtimer_type type,
	     enum alarmtimer_restart (*function)(struct alarm *, ktime_t))
{
	timerqueue_init(&alarm->node);
	alarm->timer.function = alarmtimer_fired;
	alarm->function = function;
	alarm->type = type;
	alarm->state = ALARMTIMER_STATE_INACTIVE;
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
	hrtimer_init(&alarm->timer, alarm_bases[type].base_clockid,
		     HRTIMER_MODE_ABS);
	__alarm_init(alarm, type, function);
}
EXPORT_SYMBOL_GPL(alarm_init);

/**
 * alarm_start - Sets an absolute alarm to fire
 * @alarm: ptr to alarm to set
 * @start: time to run the alarm
 */
void alarm_start(struct alarm *alarm, ktime_t start)
{
	struct alarm_base *base = &alarm_bases[alarm->type];
	unsigned long flags;

	spin_lock_irqsave(&base->lock, flags);
	alarm->node.expires = start;
	alarmtimer_enqueue(base, alarm);
	hrtimer_start(&alarm->timer, alarm->node.expires, HRTIMER_MODE_ABS);
	spin_unlock_irqrestore(&base->lock, flags);

	trace_alarmtimer_start(alarm, base->get_ktime());
}
EXPORT_SYMBOL_GPL(alarm_start);

/**
 * alarm_start_relative - Sets a relative alarm to fire
 * @alarm: ptr to alarm to set
 * @start: time relative to now to run the alarm
 */
void alarm_start_relative(struct alarm *alarm, ktime_t start)
{
	struct alarm_base *base = &alarm_bases[alarm->type];

	start = ktime_add_safe(start, base->get_ktime());
	alarm_start(alarm, start);
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

	trace_alarmtimer_cancel(alarm, base->get_ktime());
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
		hrtimer_cancel_wait_running(&alarm->timer);
	}
}
EXPORT_SYMBOL_GPL(alarm_cancel);


u64 alarm_forward(struct alarm *alarm, ktime_t now, ktime_t interval)
{
	u64 overrun = 1;
	ktime_t delta;

	delta = ktime_sub(now, alarm->node.expires);

	if (delta < 0)
		return 0;

	if (unlikely(delta >= interval)) {
		s64 incr = ktime_to_ns(interval);

		overrun = ktime_divns(delta, incr);

		alarm->node.expires = ktime_add_ns(alarm->node.expires,
							incr*overrun);

		if (alarm->node.expires > now)
			return overrun;
		/*
		 * This (and the ktime_add() below) is the
		 * correction for exact:
		 */
		overrun++;
	}

	alarm->node.expires = ktime_add_safe(alarm->node.expires, interval);
	return overrun;
}
EXPORT_SYMBOL_GPL(alarm_forward);

static u64 __alarm_forward_now(struct alarm *alarm, ktime_t interval, bool throttle)
{
	struct alarm_base *base = &alarm_bases[alarm->type];
	ktime_t now = base->get_ktime();

	if (IS_ENABLED(CONFIG_HIGH_RES_TIMERS) && throttle) {
		/*
		 * Same issue as with posix_timer_fn(). Timers which are
		 * periodic but the signal is ignored can starve the system
		 * with a very small interval. The real fix which was
		 * promised in the context of posix_timer_fn() never
		 * materialized, but someone should really work on it.
		 *
		 * To prevent DOS fake @now to be 1 jiffie out which keeps
		 * the overrun accounting correct but creates an
		 * inconsistency vs. timer_gettime(2).
		 */
		ktime_t kj = NSEC_PER_SEC / HZ;

		if (interval < kj)
			now = ktime_add(now, kj);
	}

	return alarm_forward(alarm, now, interval);
}

u64 alarm_forward_now(struct alarm *alarm, ktime_t interval)
{
	return __alarm_forward_now(alarm, interval, false);
}
EXPORT_SYMBOL_GPL(alarm_forward_now);

#ifdef CONFIG_POSIX_TIMERS

static void alarmtimer_freezerset(ktime_t absexp, enum alarmtimer_type type)
{
	struct alarm_base *base;
	unsigned long flags;
	ktime_t delta;

	switch(type) {
	case ALARM_REALTIME:
		base = &alarm_bases[ALARM_REALTIME];
		type = ALARM_REALTIME_FREEZER;
		break;
	case ALARM_BOOTTIME:
		base = &alarm_bases[ALARM_BOOTTIME];
		type = ALARM_BOOTTIME_FREEZER;
		break;
	default:
		WARN_ONCE(1, "Invalid alarm type: %d\n", type);
		return;
	}

	delta = ktime_sub(absexp, base->get_ktime());

	spin_lock_irqsave(&freezer_delta_lock, flags);
	if (!freezer_delta || (delta < freezer_delta)) {
		freezer_delta = delta;
		freezer_expires = absexp;
		freezer_alarmtype = type;
	}
	spin_unlock_irqrestore(&freezer_delta_lock, flags);
}

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
 * @now: time at the timer expiration
 *
 * Posix timer callback for expired alarm timers.
 *
 * Return: whether the timer is to be restarted
 */
static enum alarmtimer_restart alarm_handle_timer(struct alarm *alarm,
							ktime_t now)
{
	struct k_itimer *ptr = container_of(alarm, struct k_itimer,
					    it.alarm.alarmtimer);
	enum alarmtimer_restart result = ALARMTIMER_NORESTART;
	unsigned long flags;
	int si_private = 0;

	spin_lock_irqsave(&ptr->it_lock, flags);

	ptr->it_active = 0;
	if (ptr->it_interval)
		si_private = ++ptr->it_requeue_pending;

	if (posix_timer_event(ptr, si_private) && ptr->it_interval) {
		/*
		 * Handle ignored signals and rearm the timer. This will go
		 * away once we handle ignored signals proper. Ensure that
		 * small intervals cannot starve the system.
		 */
		ptr->it_overrun += __alarm_forward_now(alarm, ptr->it_interval, true);
		++ptr->it_requeue_pending;
		ptr->it_active = 1;
		result = ALARMTIMER_RESTART;
	}
	spin_unlock_irqrestore(&ptr->it_lock, flags);

	return result;
}

/**
 * alarm_timer_rearm - Posix timer callback for rearming timer
 * @timr:	Pointer to the posixtimer data struct
 */
static void alarm_timer_rearm(struct k_itimer *timr)
{
	struct alarm *alarm = &timr->it.alarm.alarmtimer;

	timr->it_overrun += alarm_forward_now(alarm, timr->it_interval);
	alarm_start(alarm, alarm->node.expires);
}

/**
 * alarm_timer_forward - Posix timer callback for forwarding timer
 * @timr:	Pointer to the posixtimer data struct
 * @now:	Current time to forward the timer against
 */
static s64 alarm_timer_forward(struct k_itimer *timr, ktime_t now)
{
	struct alarm *alarm = &timr->it.alarm.alarmtimer;

	return alarm_forward(alarm, timr->it_interval, now);
}

/**
 * alarm_timer_remaining - Posix timer callback to retrieve remaining time
 * @timr:	Pointer to the posixtimer data struct
 * @now:	Current time to calculate against
 */
static ktime_t alarm_timer_remaining(struct k_itimer *timr, ktime_t now)
{
	struct alarm *alarm = &timr->it.alarm.alarmtimer;

	return ktime_sub(alarm->node.expires, now);
}

/**
 * alarm_timer_try_to_cancel - Posix timer callback to cancel a timer
 * @timr:	Pointer to the posixtimer data struct
 */
static int alarm_timer_try_to_cancel(struct k_itimer *timr)
{
	return alarm_try_to_cancel(&timr->it.alarm.alarmtimer);
}

/**
 * alarm_timer_wait_running - Posix timer callback to wait for a timer
 * @timr:	Pointer to the posixtimer data struct
 *
 * Called from the core code when timer cancel detected that the callback
 * is running. @timr is unlocked and rcu read lock is held to prevent it
 * from being freed.
 */
static void alarm_timer_wait_running(struct k_itimer *timr)
{
	hrtimer_cancel_wait_running(&timr->it.alarm.alarmtimer.timer);
}

/**
 * alarm_timer_arm - Posix timer callback to arm a timer
 * @timr:	Pointer to the posixtimer data struct
 * @expires:	The new expiry time
 * @absolute:	Expiry value is absolute time
 * @sigev_none:	Posix timer does not deliver signals
 */
static void alarm_timer_arm(struct k_itimer *timr, ktime_t expires,
			    bool absolute, bool sigev_none)
{
	struct alarm *alarm = &timr->it.alarm.alarmtimer;
	struct alarm_base *base = &alarm_bases[alarm->type];

	if (!absolute)
		expires = ktime_add_safe(expires, base->get_ktime());
	if (sigev_none)
		alarm->node.expires = expires;
	else
		alarm_start(&timr->it.alarm.alarmtimer, expires);
}

/**
 * alarm_clock_getres - posix getres interface
 * @which_clock: clockid
 * @tp: timespec to fill
 *
 * Returns the granularity of underlying alarm base clock
 */
static int alarm_clock_getres(const clockid_t which_clock, struct timespec64 *tp)
{
	if (!alarmtimer_get_rtcdev())
		return -EINVAL;

	tp->tv_sec = 0;
	tp->tv_nsec = hrtimer_resolution;
	return 0;
}

/**
 * alarm_clock_get_timespec - posix clock_get_timespec interface
 * @which_clock: clockid
 * @tp: timespec to fill.
 *
 * Provides the underlying alarm base time in a tasks time namespace.
 */
static int alarm_clock_get_timespec(clockid_t which_clock, struct timespec64 *tp)
{
	struct alarm_base *base = &alarm_bases[clock2alarm(which_clock)];

	if (!alarmtimer_get_rtcdev())
		return -EINVAL;

	base->get_timespec(tp);

	return 0;
}

/**
 * alarm_clock_get_ktime - posix clock_get_ktime interface
 * @which_clock: clockid
 *
 * Provides the underlying alarm base time in the root namespace.
 */
static ktime_t alarm_clock_get_ktime(clockid_t which_clock)
{
	struct alarm_base *base = &alarm_bases[clock2alarm(which_clock)];

	if (!alarmtimer_get_rtcdev())
		return -EINVAL;

	return base->get_ktime();
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

	if (!alarmtimer_get_rtcdev())
		return -EOPNOTSUPP;

	if (!capable(CAP_WAKE_ALARM))
		return -EPERM;

	type = clock2alarm(new_timer->it_clock);
	alarm_init(&new_timer->it.alarm.alarmtimer, type, alarm_handle_timer);
	return 0;
}

/**
 * alarmtimer_nsleep_wakeup - Wakeup function for alarm_timer_nsleep
 * @alarm: ptr to alarm that fired
 * @now: time at the timer expiration
 *
 * Wakes up the task that set the alarmtimer
 *
 * Return: ALARMTIMER_NORESTART
 */
static enum alarmtimer_restart alarmtimer_nsleep_wakeup(struct alarm *alarm,
								ktime_t now)
{
	struct task_struct *task = alarm->data;

	alarm->data = NULL;
	if (task)
		wake_up_process(task);
	return ALARMTIMER_NORESTART;
}

/**
 * alarmtimer_do_nsleep - Internal alarmtimer nsleep implementation
 * @alarm: ptr to alarmtimer
 * @absexp: absolute expiration time
 * @type: alarm type (BOOTTIME/REALTIME).
 *
 * Sets the alarm timer and sleeps until it is fired or interrupted.
 */
static int alarmtimer_do_nsleep(struct alarm *alarm, ktime_t absexp,
				enum alarmtimer_type type)
{
	struct restart_block *restart;
	alarm->data = (void *)current;
	do {
		set_current_state(TASK_INTERRUPTIBLE);
		alarm_start(alarm, absexp);
		if (likely(alarm->data))
			schedule();

		alarm_cancel(alarm);
	} while (alarm->data && !signal_pending(current));

	__set_current_state(TASK_RUNNING);

	destroy_hrtimer_on_stack(&alarm->timer);

	if (!alarm->data)
		return 0;

	if (freezing(current))
		alarmtimer_freezerset(absexp, type);
	restart = &current->restart_block;
	if (restart->nanosleep.type != TT_NONE) {
		struct timespec64 rmt;
		ktime_t rem;

		rem = ktime_sub(absexp, alarm_bases[type].get_ktime());

		if (rem <= 0)
			return 0;
		rmt = ktime_to_timespec64(rem);

		return nanosleep_copyout(restart, &rmt);
	}
	return -ERESTART_RESTARTBLOCK;
}

static void
alarm_init_on_stack(struct alarm *alarm, enum alarmtimer_type type,
		    enum alarmtimer_restart (*function)(struct alarm *, ktime_t))
{
	hrtimer_init_on_stack(&alarm->timer, alarm_bases[type].base_clockid,
			      HRTIMER_MODE_ABS);
	__alarm_init(alarm, type, function);
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
	ktime_t exp = restart->nanosleep.expires;
	struct alarm alarm;

	alarm_init_on_stack(&alarm, type, alarmtimer_nsleep_wakeup);

	return alarmtimer_do_nsleep(&alarm, exp, type);
}

/**
 * alarm_timer_nsleep - alarmtimer nanosleep
 * @which_clock: clockid
 * @flags: determines abstime or relative
 * @tsreq: requested sleep time (abs or rel)
 *
 * Handles clock_nanosleep calls against _ALARM clockids
 */
static int alarm_timer_nsleep(const clockid_t which_clock, int flags,
			      const struct timespec64 *tsreq)
{
	enum  alarmtimer_type type = clock2alarm(which_clock);
	struct restart_block *restart = &current->restart_block;
	struct alarm alarm;
	ktime_t exp;
	int ret;

	if (!alarmtimer_get_rtcdev())
		return -EOPNOTSUPP;

	if (flags & ~TIMER_ABSTIME)
		return -EINVAL;

	if (!capable(CAP_WAKE_ALARM))
		return -EPERM;

	alarm_init_on_stack(&alarm, type, alarmtimer_nsleep_wakeup);

	exp = timespec64_to_ktime(*tsreq);
	/* Convert (if necessary) to absolute time */
	if (flags != TIMER_ABSTIME) {
		ktime_t now = alarm_bases[type].get_ktime();

		exp = ktime_add_safe(now, exp);
	} else {
		exp = timens_ktime_to_host(which_clock, exp);
	}

	ret = alarmtimer_do_nsleep(&alarm, exp, type);
	if (ret != -ERESTART_RESTARTBLOCK)
		return ret;

	/* abs timers don't set remaining time or restart */
	if (flags == TIMER_ABSTIME)
		return -ERESTARTNOHAND;

	restart->nanosleep.clockid = type;
	restart->nanosleep.expires = exp;
	set_restart_fn(restart, alarm_timer_nsleep_restart);
	return ret;
}

const struct k_clock alarm_clock = {
	.clock_getres		= alarm_clock_getres,
	.clock_get_ktime	= alarm_clock_get_ktime,
	.clock_get_timespec	= alarm_clock_get_timespec,
	.timer_create		= alarm_timer_create,
	.timer_set		= common_timer_set,
	.timer_del		= common_timer_del,
	.timer_get		= common_timer_get,
	.timer_arm		= alarm_timer_arm,
	.timer_rearm		= alarm_timer_rearm,
	.timer_forward		= alarm_timer_forward,
	.timer_remaining	= alarm_timer_remaining,
	.timer_try_to_cancel	= alarm_timer_try_to_cancel,
	.timer_wait_running	= alarm_timer_wait_running,
	.nsleep			= alarm_timer_nsleep,
};
#endif /* CONFIG_POSIX_TIMERS */


/* Suspend hook structures */
static const struct dev_pm_ops alarmtimer_pm_ops = {
	.suspend = alarmtimer_suspend,
	.resume = alarmtimer_resume,
};

static struct platform_driver alarmtimer_driver = {
	.driver = {
		.name = "alarmtimer",
		.pm = &alarmtimer_pm_ops,
	}
};

static void get_boottime_timespec(struct timespec64 *tp)
{
	ktime_get_boottime_ts64(tp);
	timens_add_boottime(tp);
}

/**
 * alarmtimer_init - Initialize alarm timer code
 *
 * This function initializes the alarm bases and registers
 * the posix clock ids.
 */
static int __init alarmtimer_init(void)
{
	int error;
	int i;

	alarmtimer_rtc_timer_init();

	/* Initialize alarm bases */
	alarm_bases[ALARM_REALTIME].base_clockid = CLOCK_REALTIME;
	alarm_bases[ALARM_REALTIME].get_ktime = &ktime_get_real;
	alarm_bases[ALARM_REALTIME].get_timespec = ktime_get_real_ts64;
	alarm_bases[ALARM_BOOTTIME].base_clockid = CLOCK_BOOTTIME;
	alarm_bases[ALARM_BOOTTIME].get_ktime = &ktime_get_boottime;
	alarm_bases[ALARM_BOOTTIME].get_timespec = get_boottime_timespec;
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

	return 0;
out_if:
	alarmtimer_rtc_interface_remove();
	return error;
}
device_initcall(alarmtimer_init);
