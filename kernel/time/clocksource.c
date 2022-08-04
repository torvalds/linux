// SPDX-License-Identifier: GPL-2.0+
/*
 * This file contains the functions which manage clocksource drivers.
 *
 * Copyright (C) 2004, 2005 IBM, John Stultz (johnstul@us.ibm.com)
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/device.h>
#include <linux/clocksource.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/sched.h> /* for spin_unlock_irq() using preempt_count() m68k */
#include <linux/tick.h>
#include <linux/kthread.h>

#include "tick-internal.h"
#include "timekeeping_internal.h"

/**
 * clocks_calc_mult_shift - calculate mult/shift factors for scaled math of clocks
 * @mult:	pointer to mult variable
 * @shift:	pointer to shift variable
 * @from:	frequency to convert from
 * @to:		frequency to convert to
 * @maxsec:	guaranteed runtime conversion range in seconds
 *
 * The function evaluates the shift/mult pair for the scaled math
 * operations of clocksources and clockevents.
 *
 * @to and @from are frequency values in HZ. For clock sources @to is
 * NSEC_PER_SEC == 1GHz and @from is the counter frequency. For clock
 * event @to is the counter frequency and @from is NSEC_PER_SEC.
 *
 * The @maxsec conversion range argument controls the time frame in
 * seconds which must be covered by the runtime conversion with the
 * calculated mult and shift factors. This guarantees that no 64bit
 * overflow happens when the input value of the conversion is
 * multiplied with the calculated mult factor. Larger ranges may
 * reduce the conversion accuracy by chosing smaller mult and shift
 * factors.
 */
void
clocks_calc_mult_shift(u32 *mult, u32 *shift, u32 from, u32 to, u32 maxsec)
{
	u64 tmp;
	u32 sft, sftacc= 32;

	/*
	 * Calculate the shift factor which is limiting the conversion
	 * range:
	 */
	tmp = ((u64)maxsec * from) >> 32;
	while (tmp) {
		tmp >>=1;
		sftacc--;
	}

	/*
	 * Find the conversion shift/mult pair which has the best
	 * accuracy and fits the maxsec conversion range:
	 */
	for (sft = 32; sft > 0; sft--) {
		tmp = (u64) to << sft;
		tmp += from / 2;
		do_div(tmp, from);
		if ((tmp >> sftacc) == 0)
			break;
	}
	*mult = tmp;
	*shift = sft;
}
EXPORT_SYMBOL_GPL(clocks_calc_mult_shift);

/*[Clocksource internal variables]---------
 * curr_clocksource:
 *	currently selected clocksource.
 * suspend_clocksource:
 *	used to calculate the suspend time.
 * clocksource_list:
 *	linked list with the registered clocksources
 * clocksource_mutex:
 *	protects manipulations to curr_clocksource and the clocksource_list
 * override_name:
 *	Name of the user-specified clocksource.
 */
static struct clocksource *curr_clocksource;
static struct clocksource *suspend_clocksource;
static LIST_HEAD(clocksource_list);
static DEFINE_MUTEX(clocksource_mutex);
static char override_name[CS_NAME_LEN];
static int finished_booting;
static u64 suspend_start;

#ifdef CONFIG_CLOCKSOURCE_WATCHDOG
static void clocksource_watchdog_work(struct work_struct *work);
static void clocksource_select(void);

static LIST_HEAD(watchdog_list);
static struct clocksource *watchdog;
static struct timer_list watchdog_timer;
static DECLARE_WORK(watchdog_work, clocksource_watchdog_work);
static DEFINE_SPINLOCK(watchdog_lock);
static int watchdog_running;
static atomic_t watchdog_reset_pending;

static inline void clocksource_watchdog_lock(unsigned long *flags)
{
	spin_lock_irqsave(&watchdog_lock, *flags);
}

static inline void clocksource_watchdog_unlock(unsigned long *flags)
{
	spin_unlock_irqrestore(&watchdog_lock, *flags);
}

static int clocksource_watchdog_kthread(void *data);
static void __clocksource_change_rating(struct clocksource *cs, int rating);

/*
 * Interval: 0.5sec Threshold: 0.0625s
 */
#define WATCHDOG_INTERVAL (HZ >> 1)
#define WATCHDOG_THRESHOLD (NSEC_PER_SEC >> 4)

static void clocksource_watchdog_work(struct work_struct *work)
{
	/*
	 * We cannot directly run clocksource_watchdog_kthread() here, because
	 * clocksource_select() calls timekeeping_notify() which uses
	 * stop_machine(). One cannot use stop_machine() from a workqueue() due
	 * lock inversions wrt CPU hotplug.
	 *
	 * Also, we only ever run this work once or twice during the lifetime
	 * of the kernel, so there is no point in creating a more permanent
	 * kthread for this.
	 *
	 * If kthread_run fails the next watchdog scan over the
	 * watchdog_list will find the unstable clock again.
	 */
	kthread_run(clocksource_watchdog_kthread, NULL, "kwatchdog");
}

static void __clocksource_unstable(struct clocksource *cs)
{
	cs->flags &= ~(CLOCK_SOURCE_VALID_FOR_HRES | CLOCK_SOURCE_WATCHDOG);
	cs->flags |= CLOCK_SOURCE_UNSTABLE;

	/*
	 * If the clocksource is registered clocksource_watchdog_kthread() will
	 * re-rate and re-select.
	 */
	if (list_empty(&cs->list)) {
		cs->rating = 0;
		return;
	}

	if (cs->mark_unstable)
		cs->mark_unstable(cs);

	/* kick clocksource_watchdog_kthread() */
	if (finished_booting)
		schedule_work(&watchdog_work);
}

/**
 * clocksource_mark_unstable - mark clocksource unstable via watchdog
 * @cs:		clocksource to be marked unstable
 *
 * This function is called by the x86 TSC code to mark clocksources as unstable;
 * it defers demotion and re-selection to a kthread.
 */
void clocksource_mark_unstable(struct clocksource *cs)
{
	unsigned long flags;

	spin_lock_irqsave(&watchdog_lock, flags);
	if (!(cs->flags & CLOCK_SOURCE_UNSTABLE)) {
		if (!list_empty(&cs->list) && list_empty(&cs->wd_list))
			list_add(&cs->wd_list, &watchdog_list);
		__clocksource_unstable(cs);
	}
	spin_unlock_irqrestore(&watchdog_lock, flags);
}

static void clocksource_watchdog(struct timer_list *unused)
{
	struct clocksource *cs;
	u64 csnow, wdnow, cslast, wdlast, delta;
	int64_t wd_nsec, cs_nsec;
	int next_cpu, reset_pending;

	spin_lock(&watchdog_lock);
	if (!watchdog_running)
		goto out;

	reset_pending = atomic_read(&watchdog_reset_pending);

	list_for_each_entry(cs, &watchdog_list, wd_list) {

		/* Clocksource already marked unstable? */
		if (cs->flags & CLOCK_SOURCE_UNSTABLE) {
			if (finished_booting)
				schedule_work(&watchdog_work);
			continue;
		}

		local_irq_disable();
		csnow = cs->read(cs);
		wdnow = watchdog->read(watchdog);
		local_irq_enable();

		/* Clocksource initialized ? */
		if (!(cs->flags & CLOCK_SOURCE_WATCHDOG) ||
		    atomic_read(&watchdog_reset_pending)) {
			cs->flags |= CLOCK_SOURCE_WATCHDOG;
			cs->wd_last = wdnow;
			cs->cs_last = csnow;
			continue;
		}

		delta = clocksource_delta(wdnow, cs->wd_last, watchdog->mask);
		wd_nsec = clocksource_cyc2ns(delta, watchdog->mult,
					     watchdog->shift);

		delta = clocksource_delta(csnow, cs->cs_last, cs->mask);
		cs_nsec = clocksource_cyc2ns(delta, cs->mult, cs->shift);
		wdlast = cs->wd_last; /* save these in case we print them */
		cslast = cs->cs_last;
		cs->cs_last = csnow;
		cs->wd_last = wdnow;

		if (atomic_read(&watchdog_reset_pending))
			continue;

		/* Check the deviation from the watchdog clocksource. */
		if (abs(cs_nsec - wd_nsec) > WATCHDOG_THRESHOLD) {
			pr_warn("timekeeping watchdog on CPU%d: Marking clocksource '%s' as unstable because the skew is too large:\n",
				smp_processor_id(), cs->name);
			pr_warn("                      '%s' wd_now: %llx wd_last: %llx mask: %llx\n",
				watchdog->name, wdnow, wdlast, watchdog->mask);
			pr_warn("                      '%s' cs_now: %llx cs_last: %llx mask: %llx\n",
				cs->name, csnow, cslast, cs->mask);
			__clocksource_unstable(cs);
			continue;
		}

		if (cs == curr_clocksource && cs->tick_stable)
			cs->tick_stable(cs);

		if (!(cs->flags & CLOCK_SOURCE_VALID_FOR_HRES) &&
		    (cs->flags & CLOCK_SOURCE_IS_CONTINUOUS) &&
		    (watchdog->flags & CLOCK_SOURCE_IS_CONTINUOUS)) {
			/* Mark it valid for high-res. */
			cs->flags |= CLOCK_SOURCE_VALID_FOR_HRES;

			/*
			 * clocksource_done_booting() will sort it if
			 * finished_booting is not set yet.
			 */
			if (!finished_booting)
				continue;

			/*
			 * If this is not the current clocksource let
			 * the watchdog thread reselect it. Due to the
			 * change to high res this clocksource might
			 * be preferred now. If it is the current
			 * clocksource let the tick code know about
			 * that change.
			 */
			if (cs != curr_clocksource) {
				cs->flags |= CLOCK_SOURCE_RESELECT;
				schedule_work(&watchdog_work);
			} else {
				tick_clock_notify();
			}
		}
	}

	/*
	 * We only clear the watchdog_reset_pending, when we did a
	 * full cycle through all clocksources.
	 */
	if (reset_pending)
		atomic_dec(&watchdog_reset_pending);

	/*
	 * Cycle through CPUs to check if the CPUs stay synchronized
	 * to each other.
	 */
	next_cpu = cpumask_next(raw_smp_processor_id(), cpu_online_mask);
	if (next_cpu >= nr_cpu_ids)
		next_cpu = cpumask_first(cpu_online_mask);

	/*
	 * Arm timer if not already pending: could race with concurrent
	 * pair clocksource_stop_watchdog() clocksource_start_watchdog().
	 */
	if (!timer_pending(&watchdog_timer)) {
		watchdog_timer.expires += WATCHDOG_INTERVAL;
		add_timer_on(&watchdog_timer, next_cpu);
	}
out:
	spin_unlock(&watchdog_lock);
}

static inline void clocksource_start_watchdog(void)
{
	if (watchdog_running || !watchdog || list_empty(&watchdog_list))
		return;
	timer_setup(&watchdog_timer, clocksource_watchdog, 0);
	watchdog_timer.expires = jiffies + WATCHDOG_INTERVAL;
	add_timer_on(&watchdog_timer, cpumask_first(cpu_online_mask));
	watchdog_running = 1;
}

static inline void clocksource_stop_watchdog(void)
{
	if (!watchdog_running || (watchdog && !list_empty(&watchdog_list)))
		return;
	del_timer(&watchdog_timer);
	watchdog_running = 0;
}

static inline void clocksource_reset_watchdog(void)
{
	struct clocksource *cs;

	list_for_each_entry(cs, &watchdog_list, wd_list)
		cs->flags &= ~CLOCK_SOURCE_WATCHDOG;
}

static void clocksource_resume_watchdog(void)
{
	atomic_inc(&watchdog_reset_pending);
}

static void clocksource_enqueue_watchdog(struct clocksource *cs)
{
	INIT_LIST_HEAD(&cs->wd_list);

	if (cs->flags & CLOCK_SOURCE_MUST_VERIFY) {
		/* cs is a clocksource to be watched. */
		list_add(&cs->wd_list, &watchdog_list);
		cs->flags &= ~CLOCK_SOURCE_WATCHDOG;
	} else {
		/* cs is a watchdog. */
		if (cs->flags & CLOCK_SOURCE_IS_CONTINUOUS)
			cs->flags |= CLOCK_SOURCE_VALID_FOR_HRES;
	}
}

static void clocksource_select_watchdog(bool fallback)
{
	struct clocksource *cs, *old_wd;
	unsigned long flags;

	spin_lock_irqsave(&watchdog_lock, flags);
	/* save current watchdog */
	old_wd = watchdog;
	if (fallback)
		watchdog = NULL;

	list_for_each_entry(cs, &clocksource_list, list) {
		/* cs is a clocksource to be watched. */
		if (cs->flags & CLOCK_SOURCE_MUST_VERIFY)
			continue;

		/* Skip current if we were requested for a fallback. */
		if (fallback && cs == old_wd)
			continue;

		/* Pick the best watchdog. */
		if (!watchdog || cs->rating > watchdog->rating)
			watchdog = cs;
	}
	/* If we failed to find a fallback restore the old one. */
	if (!watchdog)
		watchdog = old_wd;

	/* If we changed the watchdog we need to reset cycles. */
	if (watchdog != old_wd)
		clocksource_reset_watchdog();

	/* Check if the watchdog timer needs to be started. */
	clocksource_start_watchdog();
	spin_unlock_irqrestore(&watchdog_lock, flags);
}

static void clocksource_dequeue_watchdog(struct clocksource *cs)
{
	if (cs != watchdog) {
		if (cs->flags & CLOCK_SOURCE_MUST_VERIFY) {
			/* cs is a watched clocksource. */
			list_del_init(&cs->wd_list);
			/* Check if the watchdog timer needs to be stopped. */
			clocksource_stop_watchdog();
		}
	}
}

static int __clocksource_watchdog_kthread(void)
{
	struct clocksource *cs, *tmp;
	unsigned long flags;
	int select = 0;

	spin_lock_irqsave(&watchdog_lock, flags);
	list_for_each_entry_safe(cs, tmp, &watchdog_list, wd_list) {
		if (cs->flags & CLOCK_SOURCE_UNSTABLE) {
			list_del_init(&cs->wd_list);
			__clocksource_change_rating(cs, 0);
			select = 1;
		}
		if (cs->flags & CLOCK_SOURCE_RESELECT) {
			cs->flags &= ~CLOCK_SOURCE_RESELECT;
			select = 1;
		}
	}
	/* Check if the watchdog timer needs to be stopped. */
	clocksource_stop_watchdog();
	spin_unlock_irqrestore(&watchdog_lock, flags);

	return select;
}

static int clocksource_watchdog_kthread(void *data)
{
	mutex_lock(&clocksource_mutex);
	if (__clocksource_watchdog_kthread())
		clocksource_select();
	mutex_unlock(&clocksource_mutex);
	return 0;
}

static bool clocksource_is_watchdog(struct clocksource *cs)
{
	return cs == watchdog;
}

#else /* CONFIG_CLOCKSOURCE_WATCHDOG */

static void clocksource_enqueue_watchdog(struct clocksource *cs)
{
	if (cs->flags & CLOCK_SOURCE_IS_CONTINUOUS)
		cs->flags |= CLOCK_SOURCE_VALID_FOR_HRES;
}

static void clocksource_select_watchdog(bool fallback) { }
static inline void clocksource_dequeue_watchdog(struct clocksource *cs) { }
static inline void clocksource_resume_watchdog(void) { }
static inline int __clocksource_watchdog_kthread(void) { return 0; }
static bool clocksource_is_watchdog(struct clocksource *cs) { return false; }
void clocksource_mark_unstable(struct clocksource *cs) { }

static inline void clocksource_watchdog_lock(unsigned long *flags) { }
static inline void clocksource_watchdog_unlock(unsigned long *flags) { }

#endif /* CONFIG_CLOCKSOURCE_WATCHDOG */

static bool clocksource_is_suspend(struct clocksource *cs)
{
	return cs == suspend_clocksource;
}

static void __clocksource_suspend_select(struct clocksource *cs)
{
	/*
	 * Skip the clocksource which will be stopped in suspend state.
	 */
	if (!(cs->flags & CLOCK_SOURCE_SUSPEND_NONSTOP))
		return;

	/*
	 * The nonstop clocksource can be selected as the suspend clocksource to
	 * calculate the suspend time, so it should not supply suspend/resume
	 * interfaces to suspend the nonstop clocksource when system suspends.
	 */
	if (cs->suspend || cs->resume) {
		pr_warn("Nonstop clocksource %s should not supply suspend/resume interfaces\n",
			cs->name);
	}

	/* Pick the best rating. */
	if (!suspend_clocksource || cs->rating > suspend_clocksource->rating)
		suspend_clocksource = cs;
}

/**
 * clocksource_suspend_select - Select the best clocksource for suspend timing
 * @fallback:	if select a fallback clocksource
 */
static void clocksource_suspend_select(bool fallback)
{
	struct clocksource *cs, *old_suspend;

	old_suspend = suspend_clocksource;
	if (fallback)
		suspend_clocksource = NULL;

	list_for_each_entry(cs, &clocksource_list, list) {
		/* Skip current if we were requested for a fallback. */
		if (fallback && cs == old_suspend)
			continue;

		__clocksource_suspend_select(cs);
	}
}

/**
 * clocksource_start_suspend_timing - Start measuring the suspend timing
 * @cs:			current clocksource from timekeeping
 * @start_cycles:	current cycles from timekeeping
 *
 * This function will save the start cycle values of suspend timer to calculate
 * the suspend time when resuming system.
 *
 * This function is called late in the suspend process from timekeeping_suspend(),
 * that means processes are freezed, non-boot cpus and interrupts are disabled
 * now. It is therefore possible to start the suspend timer without taking the
 * clocksource mutex.
 */
void clocksource_start_suspend_timing(struct clocksource *cs, u64 start_cycles)
{
	if (!suspend_clocksource)
		return;

	/*
	 * If current clocksource is the suspend timer, we should use the
	 * tkr_mono.cycle_last value as suspend_start to avoid same reading
	 * from suspend timer.
	 */
	if (clocksource_is_suspend(cs)) {
		suspend_start = start_cycles;
		return;
	}

	if (suspend_clocksource->enable &&
	    suspend_clocksource->enable(suspend_clocksource)) {
		pr_warn_once("Failed to enable the non-suspend-able clocksource.\n");
		return;
	}

	suspend_start = suspend_clocksource->read(suspend_clocksource);
}

/**
 * clocksource_stop_suspend_timing - Stop measuring the suspend timing
 * @cs:		current clocksource from timekeeping
 * @cycle_now:	current cycles from timekeeping
 *
 * This function will calculate the suspend time from suspend timer.
 *
 * Returns nanoseconds since suspend started, 0 if no usable suspend clocksource.
 *
 * This function is called early in the resume process from timekeeping_resume(),
 * that means there is only one cpu, no processes are running and the interrupts
 * are disabled. It is therefore possible to stop the suspend timer without
 * taking the clocksource mutex.
 */
u64 clocksource_stop_suspend_timing(struct clocksource *cs, u64 cycle_now)
{
	u64 now, delta, nsec = 0;

	if (!suspend_clocksource)
		return 0;

	/*
	 * If current clocksource is the suspend timer, we should use the
	 * tkr_mono.cycle_last value from timekeeping as current cycle to
	 * avoid same reading from suspend timer.
	 */
	if (clocksource_is_suspend(cs))
		now = cycle_now;
	else
		now = suspend_clocksource->read(suspend_clocksource);

	if (now > suspend_start) {
		delta = clocksource_delta(now, suspend_start,
					  suspend_clocksource->mask);
		nsec = mul_u64_u32_shr(delta, suspend_clocksource->mult,
				       suspend_clocksource->shift);
	}

	/*
	 * Disable the suspend timer to save power if current clocksource is
	 * not the suspend timer.
	 */
	if (!clocksource_is_suspend(cs) && suspend_clocksource->disable)
		suspend_clocksource->disable(suspend_clocksource);

	return nsec;
}

/**
 * clocksource_suspend - suspend the clocksource(s)
 */
void clocksource_suspend(void)
{
	struct clocksource *cs;

	list_for_each_entry_reverse(cs, &clocksource_list, list)
		if (cs->suspend)
			cs->suspend(cs);
}

/**
 * clocksource_resume - resume the clocksource(s)
 */
void clocksource_resume(void)
{
	struct clocksource *cs;

	list_for_each_entry(cs, &clocksource_list, list)
		if (cs->resume)
			cs->resume(cs);

	clocksource_resume_watchdog();
}

/**
 * clocksource_touch_watchdog - Update watchdog
 *
 * Update the watchdog after exception contexts such as kgdb so as not
 * to incorrectly trip the watchdog. This might fail when the kernel
 * was stopped in code which holds watchdog_lock.
 */
void clocksource_touch_watchdog(void)
{
	clocksource_resume_watchdog();
}

/**
 * clocksource_max_adjustment- Returns max adjustment amount
 * @cs:         Pointer to clocksource
 *
 */
static u32 clocksource_max_adjustment(struct clocksource *cs)
{
	u64 ret;
	/*
	 * We won't try to correct for more than 11% adjustments (110,000 ppm),
	 */
	ret = (u64)cs->mult * 11;
	do_div(ret,100);
	return (u32)ret;
}

/**
 * clocks_calc_max_nsecs - Returns maximum nanoseconds that can be converted
 * @mult:	cycle to nanosecond multiplier
 * @shift:	cycle to nanosecond divisor (power of two)
 * @maxadj:	maximum adjustment value to mult (~11%)
 * @mask:	bitmask for two's complement subtraction of non 64 bit counters
 * @max_cyc:	maximum cycle value before potential overflow (does not include
 *		any safety margin)
 *
 * NOTE: This function includes a safety margin of 50%, in other words, we
 * return half the number of nanoseconds the hardware counter can technically
 * cover. This is done so that we can potentially detect problems caused by
 * delayed timers or bad hardware, which might result in time intervals that
 * are larger than what the math used can handle without overflows.
 */
u64 clocks_calc_max_nsecs(u32 mult, u32 shift, u32 maxadj, u64 mask, u64 *max_cyc)
{
	u64 max_nsecs, max_cycles;

	/*
	 * Calculate the maximum number of cycles that we can pass to the
	 * cyc2ns() function without overflowing a 64-bit result.
	 */
	max_cycles = ULLONG_MAX;
	do_div(max_cycles, mult+maxadj);

	/*
	 * The actual maximum number of cycles we can defer the clocksource is
	 * determined by the minimum of max_cycles and mask.
	 * Note: Here we subtract the maxadj to make sure we don't sleep for
	 * too long if there's a large negative adjustment.
	 */
	max_cycles = min(max_cycles, mask);
	max_nsecs = clocksource_cyc2ns(max_cycles, mult - maxadj, shift);

	/* return the max_cycles value as well if requested */
	if (max_cyc)
		*max_cyc = max_cycles;

	/* Return 50% of the actual maximum, so we can detect bad values */
	max_nsecs >>= 1;

	return max_nsecs;
}

/**
 * clocksource_update_max_deferment - Updates the clocksource max_idle_ns & max_cycles
 * @cs:         Pointer to clocksource to be updated
 *
 */
static inline void clocksource_update_max_deferment(struct clocksource *cs)
{
	cs->max_idle_ns = clocks_calc_max_nsecs(cs->mult, cs->shift,
						cs->maxadj, cs->mask,
						&cs->max_cycles);
}

static struct clocksource *clocksource_find_best(bool oneshot, bool skipcur)
{
	struct clocksource *cs;

	if (!finished_booting || list_empty(&clocksource_list))
		return NULL;

	/*
	 * We pick the clocksource with the highest rating. If oneshot
	 * mode is active, we pick the highres valid clocksource with
	 * the best rating.
	 */
	list_for_each_entry(cs, &clocksource_list, list) {
		if (skipcur && cs == curr_clocksource)
			continue;
		if (oneshot && !(cs->flags & CLOCK_SOURCE_VALID_FOR_HRES))
			continue;
		return cs;
	}
	return NULL;
}

static void __clocksource_select(bool skipcur)
{
	bool oneshot = tick_oneshot_mode_active();
	struct clocksource *best, *cs;

	/* Find the best suitable clocksource */
	best = clocksource_find_best(oneshot, skipcur);
	if (!best)
		return;

	if (!strlen(override_name))
		goto found;

	/* Check for the override clocksource. */
	list_for_each_entry(cs, &clocksource_list, list) {
		if (skipcur && cs == curr_clocksource)
			continue;
		if (strcmp(cs->name, override_name) != 0)
			continue;
		/*
		 * Check to make sure we don't switch to a non-highres
		 * capable clocksource if the tick code is in oneshot
		 * mode (highres or nohz)
		 */
		if (!(cs->flags & CLOCK_SOURCE_VALID_FOR_HRES) && oneshot) {
			/* Override clocksource cannot be used. */
			if (cs->flags & CLOCK_SOURCE_UNSTABLE) {
				pr_warn("Override clocksource %s is unstable and not HRT compatible - cannot switch while in HRT/NOHZ mode\n",
					cs->name);
				override_name[0] = 0;
			} else {
				/*
				 * The override cannot be currently verified.
				 * Deferring to let the watchdog check.
				 */
				pr_info("Override clocksource %s is not currently HRT compatible - deferring\n",
					cs->name);
			}
		} else
			/* Override clocksource can be used. */
			best = cs;
		break;
	}

found:
	if (curr_clocksource != best && !timekeeping_notify(best)) {
		pr_info("Switched to clocksource %s\n", best->name);
		curr_clocksource = best;
	}
}

/**
 * clocksource_select - Select the best clocksource available
 *
 * Private function. Must hold clocksource_mutex when called.
 *
 * Select the clocksource with the best rating, or the clocksource,
 * which is selected by userspace override.
 */
static void clocksource_select(void)
{
	__clocksource_select(false);
}

static void clocksource_select_fallback(void)
{
	__clocksource_select(true);
}

/*
 * clocksource_done_booting - Called near the end of core bootup
 *
 * Hack to avoid lots of clocksource churn at boot time.
 * We use fs_initcall because we want this to start before
 * device_initcall but after subsys_initcall.
 */
static int __init clocksource_done_booting(void)
{
	mutex_lock(&clocksource_mutex);
	curr_clocksource = clocksource_default_clock();
	finished_booting = 1;
	/*
	 * Run the watchdog first to eliminate unstable clock sources
	 */
	__clocksource_watchdog_kthread();
	clocksource_select();
	mutex_unlock(&clocksource_mutex);
	return 0;
}
fs_initcall(clocksource_done_booting);

/*
 * Enqueue the clocksource sorted by rating
 */
static void clocksource_enqueue(struct clocksource *cs)
{
	struct list_head *entry = &clocksource_list;
	struct clocksource *tmp;

	list_for_each_entry(tmp, &clocksource_list, list) {
		/* Keep track of the place, where to insert */
		if (tmp->rating < cs->rating)
			break;
		entry = &tmp->list;
	}
	list_add(&cs->list, entry);
}

/**
 * __clocksource_update_freq_scale - Used update clocksource with new freq
 * @cs:		clocksource to be registered
 * @scale:	Scale factor multiplied against freq to get clocksource hz
 * @freq:	clocksource frequency (cycles per second) divided by scale
 *
 * This should only be called from the clocksource->enable() method.
 *
 * This *SHOULD NOT* be called directly! Please use the
 * __clocksource_update_freq_hz() or __clocksource_update_freq_khz() helper
 * functions.
 */
void __clocksource_update_freq_scale(struct clocksource *cs, u32 scale, u32 freq)
{
	u64 sec;

	/*
	 * Default clocksources are *special* and self-define their mult/shift.
	 * But, you're not special, so you should specify a freq value.
	 */
	if (freq) {
		/*
		 * Calc the maximum number of seconds which we can run before
		 * wrapping around. For clocksources which have a mask > 32-bit
		 * we need to limit the max sleep time to have a good
		 * conversion precision. 10 minutes is still a reasonable
		 * amount. That results in a shift value of 24 for a
		 * clocksource with mask >= 40-bit and f >= 4GHz. That maps to
		 * ~ 0.06ppm granularity for NTP.
		 */
		sec = cs->mask;
		do_div(sec, freq);
		do_div(sec, scale);
		if (!sec)
			sec = 1;
		else if (sec > 600 && cs->mask > UINT_MAX)
			sec = 600;

		clocks_calc_mult_shift(&cs->mult, &cs->shift, freq,
				       NSEC_PER_SEC / scale, sec * scale);
	}
	/*
	 * Ensure clocksources that have large 'mult' values don't overflow
	 * when adjusted.
	 */
	cs->maxadj = clocksource_max_adjustment(cs);
	while (freq && ((cs->mult + cs->maxadj < cs->mult)
		|| (cs->mult - cs->maxadj > cs->mult))) {
		cs->mult >>= 1;
		cs->shift--;
		cs->maxadj = clocksource_max_adjustment(cs);
	}

	/*
	 * Only warn for *special* clocksources that self-define
	 * their mult/shift values and don't specify a freq.
	 */
	WARN_ONCE(cs->mult + cs->maxadj < cs->mult,
		"timekeeping: Clocksource %s might overflow on 11%% adjustment\n",
		cs->name);

	clocksource_update_max_deferment(cs);

	pr_info("%s: mask: 0x%llx max_cycles: 0x%llx, max_idle_ns: %lld ns\n",
		cs->name, cs->mask, cs->max_cycles, cs->max_idle_ns);
}
EXPORT_SYMBOL_GPL(__clocksource_update_freq_scale);

/**
 * __clocksource_register_scale - Used to install new clocksources
 * @cs:		clocksource to be registered
 * @scale:	Scale factor multiplied against freq to get clocksource hz
 * @freq:	clocksource frequency (cycles per second) divided by scale
 *
 * Returns -EBUSY if registration fails, zero otherwise.
 *
 * This *SHOULD NOT* be called directly! Please use the
 * clocksource_register_hz() or clocksource_register_khz helper functions.
 */
int __clocksource_register_scale(struct clocksource *cs, u32 scale, u32 freq)
{
	unsigned long flags;

	clocksource_arch_init(cs);

	if (cs->vdso_clock_mode < 0 ||
	    cs->vdso_clock_mode >= VDSO_CLOCKMODE_MAX) {
		pr_warn("clocksource %s registered with invalid VDSO mode %d. Disabling VDSO support.\n",
			cs->name, cs->vdso_clock_mode);
		cs->vdso_clock_mode = VDSO_CLOCKMODE_NONE;
	}

	/* Initialize mult/shift and max_idle_ns */
	__clocksource_update_freq_scale(cs, scale, freq);

	/* Add clocksource to the clocksource list */
	mutex_lock(&clocksource_mutex);

	clocksource_watchdog_lock(&flags);
	clocksource_enqueue(cs);
	clocksource_enqueue_watchdog(cs);
	clocksource_watchdog_unlock(&flags);

	clocksource_select();
	clocksource_select_watchdog(false);
	__clocksource_suspend_select(cs);
	mutex_unlock(&clocksource_mutex);
	return 0;
}
EXPORT_SYMBOL_GPL(__clocksource_register_scale);

static void __clocksource_change_rating(struct clocksource *cs, int rating)
{
	list_del(&cs->list);
	cs->rating = rating;
	clocksource_enqueue(cs);
}

/**
 * clocksource_change_rating - Change the rating of a registered clocksource
 * @cs:		clocksource to be changed
 * @rating:	new rating
 */
void clocksource_change_rating(struct clocksource *cs, int rating)
{
	unsigned long flags;

	mutex_lock(&clocksource_mutex);
	clocksource_watchdog_lock(&flags);
	__clocksource_change_rating(cs, rating);
	clocksource_watchdog_unlock(&flags);

	clocksource_select();
	clocksource_select_watchdog(false);
	clocksource_suspend_select(false);
	mutex_unlock(&clocksource_mutex);
}
EXPORT_SYMBOL(clocksource_change_rating);

/*
 * Unbind clocksource @cs. Called with clocksource_mutex held
 */
static int clocksource_unbind(struct clocksource *cs)
{
	unsigned long flags;

	if (clocksource_is_watchdog(cs)) {
		/* Select and try to install a replacement watchdog. */
		clocksource_select_watchdog(true);
		if (clocksource_is_watchdog(cs))
			return -EBUSY;
	}

	if (cs == curr_clocksource) {
		/* Select and try to install a replacement clock source */
		clocksource_select_fallback();
		if (curr_clocksource == cs)
			return -EBUSY;
	}

	if (clocksource_is_suspend(cs)) {
		/*
		 * Select and try to install a replacement suspend clocksource.
		 * If no replacement suspend clocksource, we will just let the
		 * clocksource go and have no suspend clocksource.
		 */
		clocksource_suspend_select(true);
	}

	clocksource_watchdog_lock(&flags);
	clocksource_dequeue_watchdog(cs);
	list_del_init(&cs->list);
	clocksource_watchdog_unlock(&flags);

	return 0;
}

/**
 * clocksource_unregister - remove a registered clocksource
 * @cs:	clocksource to be unregistered
 */
int clocksource_unregister(struct clocksource *cs)
{
	int ret = 0;

	mutex_lock(&clocksource_mutex);
	if (!list_empty(&cs->list))
		ret = clocksource_unbind(cs);
	mutex_unlock(&clocksource_mutex);
	return ret;
}
EXPORT_SYMBOL(clocksource_unregister);

#ifdef CONFIG_SYSFS
/**
 * current_clocksource_show - sysfs interface for current clocksource
 * @dev:	unused
 * @attr:	unused
 * @buf:	char buffer to be filled with clocksource list
 *
 * Provides sysfs interface for listing current clocksource.
 */
static ssize_t current_clocksource_show(struct device *dev,
					struct device_attribute *attr,
					char *buf)
{
	ssize_t count = 0;

	mutex_lock(&clocksource_mutex);
	count = snprintf(buf, PAGE_SIZE, "%s\n", curr_clocksource->name);
	mutex_unlock(&clocksource_mutex);

	return count;
}

ssize_t sysfs_get_uname(const char *buf, char *dst, size_t cnt)
{
	size_t ret = cnt;

	/* strings from sysfs write are not 0 terminated! */
	if (!cnt || cnt >= CS_NAME_LEN)
		return -EINVAL;

	/* strip of \n: */
	if (buf[cnt-1] == '\n')
		cnt--;
	if (cnt > 0)
		memcpy(dst, buf, cnt);
	dst[cnt] = 0;
	return ret;
}

/**
 * current_clocksource_store - interface for manually overriding clocksource
 * @dev:	unused
 * @attr:	unused
 * @buf:	name of override clocksource
 * @count:	length of buffer
 *
 * Takes input from sysfs interface for manually overriding the default
 * clocksource selection.
 */
static ssize_t current_clocksource_store(struct device *dev,
					 struct device_attribute *attr,
					 const char *buf, size_t count)
{
	ssize_t ret;

	mutex_lock(&clocksource_mutex);

	ret = sysfs_get_uname(buf, override_name, count);
	if (ret >= 0)
		clocksource_select();

	mutex_unlock(&clocksource_mutex);

	return ret;
}
static DEVICE_ATTR_RW(current_clocksource);

/**
 * unbind_clocksource_store - interface for manually unbinding clocksource
 * @dev:	unused
 * @attr:	unused
 * @buf:	unused
 * @count:	length of buffer
 *
 * Takes input from sysfs interface for manually unbinding a clocksource.
 */
static ssize_t unbind_clocksource_store(struct device *dev,
					struct device_attribute *attr,
					const char *buf, size_t count)
{
	struct clocksource *cs;
	char name[CS_NAME_LEN];
	ssize_t ret;

	ret = sysfs_get_uname(buf, name, count);
	if (ret < 0)
		return ret;

	ret = -ENODEV;
	mutex_lock(&clocksource_mutex);
	list_for_each_entry(cs, &clocksource_list, list) {
		if (strcmp(cs->name, name))
			continue;
		ret = clocksource_unbind(cs);
		break;
	}
	mutex_unlock(&clocksource_mutex);

	return ret ? ret : count;
}
static DEVICE_ATTR_WO(unbind_clocksource);

/**
 * available_clocksource_show - sysfs interface for listing clocksource
 * @dev:	unused
 * @attr:	unused
 * @buf:	char buffer to be filled with clocksource list
 *
 * Provides sysfs interface for listing registered clocksources
 */
static ssize_t available_clocksource_show(struct device *dev,
					  struct device_attribute *attr,
					  char *buf)
{
	struct clocksource *src;
	ssize_t count = 0;

	mutex_lock(&clocksource_mutex);
	list_for_each_entry(src, &clocksource_list, list) {
		/*
		 * Don't show non-HRES clocksource if the tick code is
		 * in one shot mode (highres=on or nohz=on)
		 */
		if (!tick_oneshot_mode_active() ||
		    (src->flags & CLOCK_SOURCE_VALID_FOR_HRES))
			count += snprintf(buf + count,
				  max((ssize_t)PAGE_SIZE - count, (ssize_t)0),
				  "%s ", src->name);
	}
	mutex_unlock(&clocksource_mutex);

	count += snprintf(buf + count,
			  max((ssize_t)PAGE_SIZE - count, (ssize_t)0), "\n");

	return count;
}
static DEVICE_ATTR_RO(available_clocksource);

static struct attribute *clocksource_attrs[] = {
	&dev_attr_current_clocksource.attr,
	&dev_attr_unbind_clocksource.attr,
	&dev_attr_available_clocksource.attr,
	NULL
};
ATTRIBUTE_GROUPS(clocksource);

static struct bus_type clocksource_subsys = {
	.name = "clocksource",
	.dev_name = "clocksource",
};

static struct device device_clocksource = {
	.id	= 0,
	.bus	= &clocksource_subsys,
	.groups	= clocksource_groups,
};

static int __init init_clocksource_sysfs(void)
{
	int error = subsys_system_register(&clocksource_subsys, NULL);

	if (!error)
		error = device_register(&device_clocksource);

	return error;
}

device_initcall(init_clocksource_sysfs);
#endif /* CONFIG_SYSFS */

/**
 * boot_override_clocksource - boot clock override
 * @str:	override name
 *
 * Takes a clocksource= boot argument and uses it
 * as the clocksource override name.
 */
static int __init boot_override_clocksource(char* str)
{
	mutex_lock(&clocksource_mutex);
	if (str)
		strlcpy(override_name, str, sizeof(override_name));
	mutex_unlock(&clocksource_mutex);
	return 1;
}

__setup("clocksource=", boot_override_clocksource);

/**
 * boot_override_clock - Compatibility layer for deprecated boot option
 * @str:	override name
 *
 * DEPRECATED! Takes a clock= boot argument and uses it
 * as the clocksource override name
 */
static int __init boot_override_clock(char* str)
{
	if (!strcmp(str, "pmtmr")) {
		pr_warn("clock=pmtmr is deprecated - use clocksource=acpi_pm\n");
		return boot_override_clocksource("acpi_pm");
	}
	pr_warn("clock= boot option is deprecated - use clocksource=xyz\n");
	return boot_override_clocksource(str);
}

__setup("clock=", boot_override_clock);
