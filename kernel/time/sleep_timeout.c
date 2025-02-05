// SPDX-License-Identifier: GPL-2.0
/*
 *  Kernel internal schedule timeout and sleeping functions
 */

#include <linux/delay.h>
#include <linux/jiffies.h>
#include <linux/timer.h>
#include <linux/sched/signal.h>
#include <linux/sched/debug.h>

#include "tick-internal.h"

/*
 * Since schedule_timeout()'s timer is defined on the stack, it must store
 * the target task on the stack as well.
 */
struct process_timer {
	struct timer_list timer;
	struct task_struct *task;
};

static void process_timeout(struct timer_list *t)
{
	struct process_timer *timeout = from_timer(timeout, t, timer);

	wake_up_process(timeout->task);
}

/**
 * schedule_timeout - sleep until timeout
 * @timeout: timeout value in jiffies
 *
 * Make the current task sleep until @timeout jiffies have elapsed.
 * The function behavior depends on the current task state
 * (see also set_current_state() description):
 *
 * %TASK_RUNNING - the scheduler is called, but the task does not sleep
 * at all. That happens because sched_submit_work() does nothing for
 * tasks in %TASK_RUNNING state.
 *
 * %TASK_UNINTERRUPTIBLE - at least @timeout jiffies are guaranteed to
 * pass before the routine returns unless the current task is explicitly
 * woken up, (e.g. by wake_up_process()).
 *
 * %TASK_INTERRUPTIBLE - the routine may return early if a signal is
 * delivered to the current task or the current task is explicitly woken
 * up.
 *
 * The current task state is guaranteed to be %TASK_RUNNING when this
 * routine returns.
 *
 * Specifying a @timeout value of %MAX_SCHEDULE_TIMEOUT will schedule
 * the CPU away without a bound on the timeout. In this case the return
 * value will be %MAX_SCHEDULE_TIMEOUT.
 *
 * Returns: 0 when the timer has expired otherwise the remaining time in
 * jiffies will be returned. In all cases the return value is guaranteed
 * to be non-negative.
 */
signed long __sched schedule_timeout(signed long timeout)
{
	struct process_timer timer;
	unsigned long expire;

	switch (timeout) {
	case MAX_SCHEDULE_TIMEOUT:
		/*
		 * These two special cases are useful to be comfortable
		 * in the caller. Nothing more. We could take
		 * MAX_SCHEDULE_TIMEOUT from one of the negative value
		 * but I' d like to return a valid offset (>=0) to allow
		 * the caller to do everything it want with the retval.
		 */
		schedule();
		goto out;
	default:
		/*
		 * Another bit of PARANOID. Note that the retval will be
		 * 0 since no piece of kernel is supposed to do a check
		 * for a negative retval of schedule_timeout() (since it
		 * should never happens anyway). You just have the printk()
		 * that will tell you if something is gone wrong and where.
		 */
		if (timeout < 0) {
			pr_err("%s: wrong timeout value %lx\n", __func__, timeout);
			dump_stack();
			__set_current_state(TASK_RUNNING);
			goto out;
		}
	}

	expire = timeout + jiffies;

	timer.task = current;
	timer_setup_on_stack(&timer.timer, process_timeout, 0);
	timer.timer.expires = expire;
	add_timer(&timer.timer);
	schedule();
	del_timer_sync(&timer.timer);

	/* Remove the timer from the object tracker */
	destroy_timer_on_stack(&timer.timer);

	timeout = expire - jiffies;

 out:
	return timeout < 0 ? 0 : timeout;
}
EXPORT_SYMBOL(schedule_timeout);

/*
 * __set_current_state() can be used in schedule_timeout_*() functions, because
 * schedule_timeout() calls schedule() unconditionally.
 */

/**
 * schedule_timeout_interruptible - sleep until timeout (interruptible)
 * @timeout: timeout value in jiffies
 *
 * See schedule_timeout() for details.
 *
 * Task state is set to TASK_INTERRUPTIBLE before starting the timeout.
 */
signed long __sched schedule_timeout_interruptible(signed long timeout)
{
	__set_current_state(TASK_INTERRUPTIBLE);
	return schedule_timeout(timeout);
}
EXPORT_SYMBOL(schedule_timeout_interruptible);

/**
 * schedule_timeout_killable - sleep until timeout (killable)
 * @timeout: timeout value in jiffies
 *
 * See schedule_timeout() for details.
 *
 * Task state is set to TASK_KILLABLE before starting the timeout.
 */
signed long __sched schedule_timeout_killable(signed long timeout)
{
	__set_current_state(TASK_KILLABLE);
	return schedule_timeout(timeout);
}
EXPORT_SYMBOL(schedule_timeout_killable);

/**
 * schedule_timeout_uninterruptible - sleep until timeout (uninterruptible)
 * @timeout: timeout value in jiffies
 *
 * See schedule_timeout() for details.
 *
 * Task state is set to TASK_UNINTERRUPTIBLE before starting the timeout.
 */
signed long __sched schedule_timeout_uninterruptible(signed long timeout)
{
	__set_current_state(TASK_UNINTERRUPTIBLE);
	return schedule_timeout(timeout);
}
EXPORT_SYMBOL(schedule_timeout_uninterruptible);

/**
 * schedule_timeout_idle - sleep until timeout (idle)
 * @timeout: timeout value in jiffies
 *
 * See schedule_timeout() for details.
 *
 * Task state is set to TASK_IDLE before starting the timeout. It is similar to
 * schedule_timeout_uninterruptible(), except this task will not contribute to
 * load average.
 */
signed long __sched schedule_timeout_idle(signed long timeout)
{
	__set_current_state(TASK_IDLE);
	return schedule_timeout(timeout);
}
EXPORT_SYMBOL(schedule_timeout_idle);

/**
 * schedule_hrtimeout_range_clock - sleep until timeout
 * @expires:	timeout value (ktime_t)
 * @delta:	slack in expires timeout (ktime_t)
 * @mode:	timer mode
 * @clock_id:	timer clock to be used
 *
 * Details are explained in schedule_hrtimeout_range() function description as
 * this function is commonly used.
 */
int __sched schedule_hrtimeout_range_clock(ktime_t *expires, u64 delta,
					   const enum hrtimer_mode mode, clockid_t clock_id)
{
	struct hrtimer_sleeper t;

	/*
	 * Optimize when a zero timeout value is given. It does not
	 * matter whether this is an absolute or a relative time.
	 */
	if (expires && *expires == 0) {
		__set_current_state(TASK_RUNNING);
		return 0;
	}

	/*
	 * A NULL parameter means "infinite"
	 */
	if (!expires) {
		schedule();
		return -EINTR;
	}

	hrtimer_setup_sleeper_on_stack(&t, clock_id, mode);
	hrtimer_set_expires_range_ns(&t.timer, *expires, delta);
	hrtimer_sleeper_start_expires(&t, mode);

	if (likely(t.task))
		schedule();

	hrtimer_cancel(&t.timer);
	destroy_hrtimer_on_stack(&t.timer);

	__set_current_state(TASK_RUNNING);

	return !t.task ? 0 : -EINTR;
}
EXPORT_SYMBOL_GPL(schedule_hrtimeout_range_clock);

/**
 * schedule_hrtimeout_range - sleep until timeout
 * @expires:	timeout value (ktime_t)
 * @delta:	slack in expires timeout (ktime_t)
 * @mode:	timer mode
 *
 * Make the current task sleep until the given expiry time has
 * elapsed. The routine will return immediately unless
 * the current task state has been set (see set_current_state()).
 *
 * The @delta argument gives the kernel the freedom to schedule the
 * actual wakeup to a time that is both power and performance friendly
 * for regular (non RT/DL) tasks.
 * The kernel give the normal best effort behavior for "@expires+@delta",
 * but may decide to fire the timer earlier, but no earlier than @expires.
 *
 * You can set the task state as follows -
 *
 * %TASK_UNINTERRUPTIBLE - at least @timeout time is guaranteed to
 * pass before the routine returns unless the current task is explicitly
 * woken up, (e.g. by wake_up_process()).
 *
 * %TASK_INTERRUPTIBLE - the routine may return early if a signal is
 * delivered to the current task or the current task is explicitly woken
 * up.
 *
 * The current task state is guaranteed to be TASK_RUNNING when this
 * routine returns.
 *
 * Returns: 0 when the timer has expired. If the task was woken before the
 * timer expired by a signal (only possible in state TASK_INTERRUPTIBLE) or
 * by an explicit wakeup, it returns -EINTR.
 */
int __sched schedule_hrtimeout_range(ktime_t *expires, u64 delta,
				     const enum hrtimer_mode mode)
{
	return schedule_hrtimeout_range_clock(expires, delta, mode,
					      CLOCK_MONOTONIC);
}
EXPORT_SYMBOL_GPL(schedule_hrtimeout_range);

/**
 * schedule_hrtimeout - sleep until timeout
 * @expires:	timeout value (ktime_t)
 * @mode:	timer mode
 *
 * See schedule_hrtimeout_range() for details. @delta argument of
 * schedule_hrtimeout_range() is set to 0 and has therefore no impact.
 */
int __sched schedule_hrtimeout(ktime_t *expires, const enum hrtimer_mode mode)
{
	return schedule_hrtimeout_range(expires, 0, mode);
}
EXPORT_SYMBOL_GPL(schedule_hrtimeout);

/**
 * msleep - sleep safely even with waitqueue interruptions
 * @msecs:	Requested sleep duration in milliseconds
 *
 * msleep() uses jiffy based timeouts for the sleep duration. Because of the
 * design of the timer wheel, the maximum additional percentage delay (slack) is
 * 12.5%. This is only valid for timers which will end up in level 1 or a higher
 * level of the timer wheel. For explanation of those 12.5% please check the
 * detailed description about the basics of the timer wheel.
 *
 * The slack of timers which will end up in level 0 depends on sleep duration
 * (msecs) and HZ configuration and can be calculated in the following way (with
 * the timer wheel design restriction that the slack is not less than 12.5%):
 *
 *   ``slack = MSECS_PER_TICK / msecs``
 *
 * When the allowed slack of the callsite is known, the calculation could be
 * turned around to find the minimal allowed sleep duration to meet the
 * constraints. For example:
 *
 * * ``HZ=1000`` with ``slack=25%``: ``MSECS_PER_TICK / slack = 1 / (1/4) = 4``:
 *   all sleep durations greater or equal 4ms will meet the constraints.
 * * ``HZ=1000`` with ``slack=12.5%``: ``MSECS_PER_TICK / slack = 1 / (1/8) = 8``:
 *   all sleep durations greater or equal 8ms will meet the constraints.
 * * ``HZ=250`` with ``slack=25%``: ``MSECS_PER_TICK / slack = 4 / (1/4) = 16``:
 *   all sleep durations greater or equal 16ms will meet the constraints.
 * * ``HZ=250`` with ``slack=12.5%``: ``MSECS_PER_TICK / slack = 4 / (1/8) = 32``:
 *   all sleep durations greater or equal 32ms will meet the constraints.
 *
 * See also the signal aware variant msleep_interruptible().
 */
void msleep(unsigned int msecs)
{
	unsigned long timeout = msecs_to_jiffies(msecs);

	while (timeout)
		timeout = schedule_timeout_uninterruptible(timeout);
}
EXPORT_SYMBOL(msleep);

/**
 * msleep_interruptible - sleep waiting for signals
 * @msecs:	Requested sleep duration in milliseconds
 *
 * See msleep() for some basic information.
 *
 * The difference between msleep() and msleep_interruptible() is that the sleep
 * could be interrupted by a signal delivery and then returns early.
 *
 * Returns: The remaining time of the sleep duration transformed to msecs (see
 * schedule_timeout() for details).
 */
unsigned long msleep_interruptible(unsigned int msecs)
{
	unsigned long timeout = msecs_to_jiffies(msecs);

	while (timeout && !signal_pending(current))
		timeout = schedule_timeout_interruptible(timeout);
	return jiffies_to_msecs(timeout);
}
EXPORT_SYMBOL(msleep_interruptible);

/**
 * usleep_range_state - Sleep for an approximate time in a given state
 * @min:	Minimum time in usecs to sleep
 * @max:	Maximum time in usecs to sleep
 * @state:	State of the current task that will be while sleeping
 *
 * usleep_range_state() sleeps at least for the minimum specified time but not
 * longer than the maximum specified amount of time. The range might reduce
 * power usage by allowing hrtimers to coalesce an already scheduled interrupt
 * with this hrtimer. In the worst case, an interrupt is scheduled for the upper
 * bound.
 *
 * The sleeping task is set to the specified state before starting the sleep.
 *
 * In non-atomic context where the exact wakeup time is flexible, use
 * usleep_range() or its variants instead of udelay(). The sleep improves
 * responsiveness by avoiding the CPU-hogging busy-wait of udelay().
 */
void __sched usleep_range_state(unsigned long min, unsigned long max, unsigned int state)
{
	ktime_t exp = ktime_add_us(ktime_get(), min);
	u64 delta = (u64)(max - min) * NSEC_PER_USEC;

	if (WARN_ON_ONCE(max < min))
		delta = 0;

	for (;;) {
		__set_current_state(state);
		/* Do not return before the requested sleep time has elapsed */
		if (!schedule_hrtimeout_range(&exp, delta, HRTIMER_MODE_ABS))
			break;
	}
}
EXPORT_SYMBOL(usleep_range_state);
