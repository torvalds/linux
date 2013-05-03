/*
 *  linux/kernel/time/tick-sched.c
 *
 *  Copyright(C) 2005-2006, Thomas Gleixner <tglx@linutronix.de>
 *  Copyright(C) 2005-2007, Red Hat, Inc., Ingo Molnar
 *  Copyright(C) 2006-2007  Timesys Corp., Thomas Gleixner
 *
 *  No idle tick implementation for low and high resolution timers
 *
 *  Started by: Thomas Gleixner and Ingo Molnar
 *
 *  Distribute under GPLv2.
 */
#include <linux/cpu.h>
#include <linux/err.h>
#include <linux/hrtimer.h>
#include <linux/interrupt.h>
#include <linux/kernel_stat.h>
#include <linux/percpu.h>
#include <linux/profile.h>
#include <linux/sched.h>
#include <linux/module.h>

#include <asm/irq_regs.h>

#include "tick-internal.h"

/*
 * Per cpu nohz control structure
 */
static DEFINE_PER_CPU(struct tick_sched, tick_cpu_sched);

/*
 * The time, when the last jiffy update happened. Protected by xtime_lock.
 */
static ktime_t last_jiffies_update;

struct tick_sched *tick_get_tick_sched(int cpu)
{
	return &per_cpu(tick_cpu_sched, cpu);
}

/*
 * Must be called with interrupts disabled !
 */
static void tick_do_update_jiffies64(ktime_t now)
{
	unsigned long ticks = 0;
	ktime_t delta;

	/*
	 * Do a quick check without holding xtime_lock:
	 */
	delta = ktime_sub(now, last_jiffies_update);
	if (delta.tv64 < tick_period.tv64)
		return;

	/* Reevalute with xtime_lock held */
	write_seqlock(&xtime_lock);

	delta = ktime_sub(now, last_jiffies_update);
	if (delta.tv64 >= tick_period.tv64) {

		delta = ktime_sub(delta, tick_period);
		last_jiffies_update = ktime_add(last_jiffies_update,
						tick_period);

		/* Slow path for long timeouts */
		if (unlikely(delta.tv64 >= tick_period.tv64)) {
			s64 incr = ktime_to_ns(tick_period);

			ticks = ktime_divns(delta, incr);

			last_jiffies_update = ktime_add_ns(last_jiffies_update,
							   incr * ticks);
		}
		do_timer(++ticks);

		/* Keep the tick_next_period variable up to date */
		tick_next_period = ktime_add(last_jiffies_update, tick_period);
	}
	write_sequnlock(&xtime_lock);
}

/*
 * Initialize and return retrieve the jiffies update.
 */
static ktime_t tick_init_jiffy_update(void)
{
	ktime_t period;

	write_seqlock(&xtime_lock);
	/* Did we start the jiffies update yet ? */
	if (last_jiffies_update.tv64 == 0)
		last_jiffies_update = tick_next_period;
	period = last_jiffies_update;
	write_sequnlock(&xtime_lock);
	return period;
}

/*
 * NOHZ - aka dynamic tick functionality
 */
#ifdef CONFIG_NO_HZ
/*
 * NO HZ enabled ?
 */
static int tick_nohz_enabled __read_mostly  = 1;

/*
 * Enable / Disable tickless mode
 */
static int __init setup_tick_nohz(char *str)
{
	if (!strcmp(str, "off"))
		tick_nohz_enabled = 0;
	else if (!strcmp(str, "on"))
		tick_nohz_enabled = 1;
	else
		return 0;
	return 1;
}

__setup("nohz=", setup_tick_nohz);

/**
 * tick_nohz_update_jiffies - update jiffies when idle was interrupted
 *
 * Called from interrupt entry when the CPU was idle
 *
 * In case the sched_tick was stopped on this CPU, we have to check if jiffies
 * must be updated. Otherwise an interrupt handler could use a stale jiffy
 * value. We do this unconditionally on any cpu, as we don't know whether the
 * cpu, which has the update task assigned is in a long sleep.
 */
static void tick_nohz_update_jiffies(ktime_t now)
{
	int cpu = smp_processor_id();
	struct tick_sched *ts = &per_cpu(tick_cpu_sched, cpu);
	unsigned long flags;

	cpumask_clear_cpu(cpu, nohz_cpu_mask);
	ts->idle_waketime = now;

	local_irq_save(flags);
	tick_do_update_jiffies64(now);
	local_irq_restore(flags);

	touch_softlockup_watchdog();
}

/*
 * Updates the per cpu time idle statistics counters
 */
static void
update_ts_time_stats(int cpu, struct tick_sched *ts, ktime_t now, u64 *last_update_time)
{
	ktime_t delta;

	if (ts->idle_active) {
		delta = ktime_sub(now, ts->idle_entrytime);
		ts->idle_sleeptime = ktime_add(ts->idle_sleeptime, delta);
		if (nr_iowait_cpu(cpu) > 0)
			ts->iowait_sleeptime = ktime_add(ts->iowait_sleeptime, delta);
		ts->idle_entrytime = now;
	}

	if (last_update_time)
		*last_update_time = ktime_to_us(now);

}

static void tick_nohz_stop_idle(int cpu, ktime_t now)
{
	struct tick_sched *ts = &per_cpu(tick_cpu_sched, cpu);

	update_ts_time_stats(cpu, ts, now, NULL);
	ts->idle_active = 0;

	sched_clock_idle_wakeup_event(0);
}

static ktime_t tick_nohz_start_idle(int cpu, struct tick_sched *ts)
{
	ktime_t now;

	now = ktime_get();

	update_ts_time_stats(cpu, ts, now, NULL);

	ts->idle_entrytime = now;
	ts->idle_active = 1;
	sched_clock_idle_sleep_event();
	return now;
}

/**
 * get_cpu_idle_time_us - get the total idle time of a cpu
 * @cpu: CPU number to query
 * @last_update_time: variable to store update time in
 *
 * Return the cummulative idle time (since boot) for a given
 * CPU, in microseconds. The idle time returned includes
 * the iowait time (unlike what "top" and co report).
 *
 * This time is measured via accounting rather than sampling,
 * and is as accurate as ktime_get() is.
 *
 * This function returns -1 if NOHZ is not enabled.
 */
u64 get_cpu_idle_time_us(int cpu, u64 *last_update_time)
{
	struct tick_sched *ts = &per_cpu(tick_cpu_sched, cpu);

	if (!tick_nohz_enabled)
		return -1;

	update_ts_time_stats(cpu, ts, ktime_get(), last_update_time);

	return ktime_to_us(ts->idle_sleeptime);
}
EXPORT_SYMBOL_GPL(get_cpu_idle_time_us);

/*
 * get_cpu_iowait_time_us - get the total iowait time of a cpu
 * @cpu: CPU number to query
 * @last_update_time: variable to store update time in
 *
 * Return the cummulative iowait time (since boot) for a given
 * CPU, in microseconds.
 *
 * This time is measured via accounting rather than sampling,
 * and is as accurate as ktime_get() is.
 *
 * This function returns -1 if NOHZ is not enabled.
 */
u64 get_cpu_iowait_time_us(int cpu, u64 *last_update_time)
{
	struct tick_sched *ts = &per_cpu(tick_cpu_sched, cpu);

	if (!tick_nohz_enabled)
		return -1;

	update_ts_time_stats(cpu, ts, ktime_get(), last_update_time);

	return ktime_to_us(ts->iowait_sleeptime);
}
EXPORT_SYMBOL_GPL(get_cpu_iowait_time_us);

/**
 * tick_nohz_stop_sched_tick - stop the idle tick from the idle task
 *
 * When the next event is more than a tick into the future, stop the idle tick
 * Called either from the idle loop or from irq_exit() when an idle period was
 * just interrupted by an interrupt which did not cause a reschedule.
 */
void tick_nohz_stop_sched_tick(int inidle)
{
	unsigned long seq, last_jiffies, next_jiffies, delta_jiffies, flags;
	struct tick_sched *ts;
	ktime_t last_update, expires, now;
	struct clock_event_device *dev = __get_cpu_var(tick_cpu_device).evtdev;
	u64 time_delta;
	int cpu;

	local_irq_save(flags);

	cpu = smp_processor_id();
	ts = &per_cpu(tick_cpu_sched, cpu);

	/*
	 * Call to tick_nohz_start_idle stops the last_update_time from being
	 * updated. Thus, it must not be called in the event we are called from
	 * irq_exit() with the prior state different than idle.
	 */
	if (!inidle && !ts->inidle)
		goto end;

	/*
	 * Set ts->inidle unconditionally. Even if the system did not
	 * switch to NOHZ mode the cpu frequency governers rely on the
	 * update of the idle time accounting in tick_nohz_start_idle().
	 */
	ts->inidle = 1;

	now = tick_nohz_start_idle(cpu, ts);

	/*
	 * If this cpu is offline and it is the one which updates
	 * jiffies, then give up the assignment and let it be taken by
	 * the cpu which runs the tick timer next. If we don't drop
	 * this here the jiffies might be stale and do_timer() never
	 * invoked.
	 */
	if (unlikely(!cpu_online(cpu))) {
		if (cpu == tick_do_timer_cpu)
			tick_do_timer_cpu = TICK_DO_TIMER_NONE;
	}

	if (unlikely(ts->nohz_mode == NOHZ_MODE_INACTIVE))
		goto end;

	if (need_resched())
		goto end;

	if (unlikely(local_softirq_pending() && cpu_online(cpu))) {
		static int ratelimit;

		if (ratelimit < 10) {
			printk(KERN_ERR "NOHZ: local_softirq_pending %02x\n",
			       (unsigned int) local_softirq_pending());
			ratelimit++;
		}
		goto end;
	}

	ts->idle_calls++;
	/* Read jiffies and the time when jiffies were updated last */
	do {
		seq = read_seqbegin(&xtime_lock);
		last_update = last_jiffies_update;
		last_jiffies = jiffies;
		time_delta = timekeeping_max_deferment();
	} while (read_seqretry(&xtime_lock, seq));

	if (rcu_needs_cpu(cpu) || printk_needs_cpu(cpu) ||
	    arch_needs_cpu(cpu)) {
		next_jiffies = last_jiffies + 1;
		delta_jiffies = 1;
	} else {
		/* Get the next timer wheel timer */
		next_jiffies = get_next_timer_interrupt(last_jiffies);
		delta_jiffies = next_jiffies - last_jiffies;
	}
	/*
	 * Do not stop the tick, if we are only one off
	 * or if the cpu is required for rcu
	 */
	if (!ts->tick_stopped && delta_jiffies == 1)
		goto out;

	/* Schedule the tick, if we are at least one jiffie off */
	if ((long)delta_jiffies >= 1) {

		/*
		 * If this cpu is the one which updates jiffies, then
		 * give up the assignment and let it be taken by the
		 * cpu which runs the tick timer next, which might be
		 * this cpu as well. If we don't drop this here the
		 * jiffies might be stale and do_timer() never
		 * invoked. Keep track of the fact that it was the one
		 * which had the do_timer() duty last. If this cpu is
		 * the one which had the do_timer() duty last, we
		 * limit the sleep time to the timekeeping
		 * max_deferement value which we retrieved
		 * above. Otherwise we can sleep as long as we want.
		 */
		if (cpu == tick_do_timer_cpu) {
			tick_do_timer_cpu = TICK_DO_TIMER_NONE;
			ts->do_timer_last = 1;
		} else if (tick_do_timer_cpu != TICK_DO_TIMER_NONE) {
			time_delta = KTIME_MAX;
			ts->do_timer_last = 0;
		} else if (!ts->do_timer_last) {
			time_delta = KTIME_MAX;
		}

		/*
		 * calculate the expiry time for the next timer wheel
		 * timer. delta_jiffies >= NEXT_TIMER_MAX_DELTA signals
		 * that there is no timer pending or at least extremely
		 * far into the future (12 days for HZ=1000). In this
		 * case we set the expiry to the end of time.
		 */
		if (likely(delta_jiffies < NEXT_TIMER_MAX_DELTA)) {
			/*
			 * Calculate the time delta for the next timer event.
			 * If the time delta exceeds the maximum time delta
			 * permitted by the current clocksource then adjust
			 * the time delta accordingly to ensure the
			 * clocksource does not wrap.
			 */
			time_delta = min_t(u64, time_delta,
					   tick_period.tv64 * delta_jiffies);
		}

		if (time_delta < KTIME_MAX)
			expires = ktime_add_ns(last_update, time_delta);
		else
			expires.tv64 = KTIME_MAX;

		if (delta_jiffies > 1)
			cpumask_set_cpu(cpu, nohz_cpu_mask);

		/* Skip reprogram of event if its not changed */
		if (ts->tick_stopped && ktime_equal(expires, dev->next_event))
			goto out;

		/*
		 * nohz_stop_sched_tick can be called several times before
		 * the nohz_restart_sched_tick is called. This happens when
		 * interrupts arrive which do not cause a reschedule. In the
		 * first call we save the current tick time, so we can restart
		 * the scheduler tick in nohz_restart_sched_tick.
		 */
		if (!ts->tick_stopped) {
			select_nohz_load_balancer(1);

			ts->idle_tick = hrtimer_get_expires(&ts->sched_timer);
			ts->tick_stopped = 1;
			ts->idle_jiffies = last_jiffies;
			rcu_enter_nohz();
		}

		ts->idle_sleeps++;

		/* Mark expires */
		ts->idle_expires = expires;

		/*
		 * If the expiration time == KTIME_MAX, then
		 * in this case we simply stop the tick timer.
		 */
		 if (unlikely(expires.tv64 == KTIME_MAX)) {
			if (ts->nohz_mode == NOHZ_MODE_HIGHRES)
				hrtimer_cancel(&ts->sched_timer);
			goto out;
		}

		if (ts->nohz_mode == NOHZ_MODE_HIGHRES) {
			hrtimer_start(&ts->sched_timer, expires,
				      HRTIMER_MODE_ABS_PINNED);
			/* Check, if the timer was already in the past */
			if (hrtimer_active(&ts->sched_timer))
				goto out;
		} else if (!tick_program_event(expires, 0))
				goto out;
		/*
		 * We are past the event already. So we crossed a
		 * jiffie boundary. Update jiffies and raise the
		 * softirq.
		 */
		tick_do_update_jiffies64(ktime_get());
		cpumask_clear_cpu(cpu, nohz_cpu_mask);
	}
	raise_softirq_irqoff(TIMER_SOFTIRQ);
out:
	ts->next_jiffies = next_jiffies;
	ts->last_jiffies = last_jiffies;
	ts->sleep_length = ktime_sub(dev->next_event, now);
end:
	local_irq_restore(flags);
}

/**
 * tick_nohz_get_sleep_length - return the length of the current sleep
 *
 * Called from power state control code with interrupts disabled
 */
ktime_t tick_nohz_get_sleep_length(void)
{
	struct tick_sched *ts = &__get_cpu_var(tick_cpu_sched);

	return ts->sleep_length;
}

static void tick_nohz_restart(struct tick_sched *ts, ktime_t now)
{
	hrtimer_cancel(&ts->sched_timer);
	hrtimer_set_expires(&ts->sched_timer, ts->idle_tick);

	while (1) {
		/* Forward the time to expire in the future */
		hrtimer_forward(&ts->sched_timer, now, tick_period);

		if (ts->nohz_mode == NOHZ_MODE_HIGHRES) {
			hrtimer_start_expires(&ts->sched_timer,
					      HRTIMER_MODE_ABS_PINNED);
			/* Check, if the timer was already in the past */
			if (hrtimer_active(&ts->sched_timer))
				break;
		} else {
			if (!tick_program_event(
				hrtimer_get_expires(&ts->sched_timer), 0))
				break;
		}
		/* Reread time and update jiffies */
		now = ktime_get();
		tick_do_update_jiffies64(now);
	}
}

/**
 * tick_nohz_restart_sched_tick - restart the idle tick from the idle task
 *
 * Restart the idle tick when the CPU is woken up from idle
 */
void tick_nohz_restart_sched_tick(void)
{
	int cpu = smp_processor_id();
	struct tick_sched *ts = &per_cpu(tick_cpu_sched, cpu);
#ifndef CONFIG_VIRT_CPU_ACCOUNTING
	unsigned long ticks;
#endif
	ktime_t now;

	local_irq_disable();
	if (ts->idle_active || (ts->inidle && ts->tick_stopped))
		now = ktime_get();

	if (ts->idle_active)
		tick_nohz_stop_idle(cpu, now);

	if (!ts->inidle || !ts->tick_stopped) {
		ts->inidle = 0;
		local_irq_enable();
		return;
	}

	ts->inidle = 0;

	rcu_exit_nohz();

	/* Update jiffies first */
	select_nohz_load_balancer(0);
	tick_do_update_jiffies64(now);
	cpumask_clear_cpu(cpu, nohz_cpu_mask);

#ifndef CONFIG_VIRT_CPU_ACCOUNTING
	/*
	 * We stopped the tick in idle. Update process times would miss the
	 * time we slept as update_process_times does only a 1 tick
	 * accounting. Enforce that this is accounted to idle !
	 */
	ticks = jiffies - ts->idle_jiffies;
	/*
	 * We might be one off. Do not randomly account a huge number of ticks!
	 */
	if (ticks && ticks < LONG_MAX)
		account_idle_ticks(ticks);
#endif

	touch_softlockup_watchdog();
	/*
	 * Cancel the scheduled timer and restore the tick
	 */
	ts->tick_stopped  = 0;
	ts->idle_exittime = now;

	tick_nohz_restart(ts, now);

	local_irq_enable();
}

static int tick_nohz_reprogram(struct tick_sched *ts, ktime_t now)
{
	hrtimer_forward(&ts->sched_timer, now, tick_period);
	return tick_program_event(hrtimer_get_expires(&ts->sched_timer), 0);
}

/*
 * The nohz low res interrupt handler
 */
static void tick_nohz_handler(struct clock_event_device *dev)
{
	struct tick_sched *ts = &__get_cpu_var(tick_cpu_sched);
	struct pt_regs *regs = get_irq_regs();
	int cpu = smp_processor_id();
	ktime_t now = ktime_get();

	dev->next_event.tv64 = KTIME_MAX;

	/*
	 * Check if the do_timer duty was dropped. We don't care about
	 * concurrency: This happens only when the cpu in charge went
	 * into a long sleep. If two cpus happen to assign themself to
	 * this duty, then the jiffies update is still serialized by
	 * xtime_lock.
	 */
	if (unlikely(tick_do_timer_cpu == TICK_DO_TIMER_NONE))
		tick_do_timer_cpu = cpu;

	/* Check, if the jiffies need an update */
	if (tick_do_timer_cpu == cpu)
		tick_do_update_jiffies64(now);

	/*
	 * When we are idle and the tick is stopped, we have to touch
	 * the watchdog as we might not schedule for a really long
	 * time. This happens on complete idle SMP systems while
	 * waiting on the login prompt. We also increment the "start
	 * of idle" jiffy stamp so the idle accounting adjustment we
	 * do when we go busy again does not account too much ticks.
	 */
	if (ts->tick_stopped) {
		touch_softlockup_watchdog();
		ts->idle_jiffies++;
	}

	update_process_times(user_mode(regs));
	profile_tick(CPU_PROFILING);

	while (tick_nohz_reprogram(ts, now)) {
		now = ktime_get();
		tick_do_update_jiffies64(now);
	}
}

/**
 * tick_nohz_switch_to_nohz - switch to nohz mode
 */
static void tick_nohz_switch_to_nohz(void)
{
	struct tick_sched *ts = &__get_cpu_var(tick_cpu_sched);
	ktime_t next;

	if (!tick_nohz_enabled)
		return;

	local_irq_disable();
	if (tick_switch_to_oneshot(tick_nohz_handler)) {
		local_irq_enable();
		return;
	}

	ts->nohz_mode = NOHZ_MODE_LOWRES;

	/*
	 * Recycle the hrtimer in ts, so we can share the
	 * hrtimer_forward with the highres code.
	 */
	hrtimer_init(&ts->sched_timer, CLOCK_MONOTONIC, HRTIMER_MODE_ABS);
	/* Get the next period */
	next = tick_init_jiffy_update();

	for (;;) {
		hrtimer_set_expires(&ts->sched_timer, next);
		if (!tick_program_event(next, 0))
			break;
		next = ktime_add(next, tick_period);
	}
	local_irq_enable();

	printk(KERN_INFO "Switched to NOHz mode on CPU #%d\n", smp_processor_id());
}

/*
 * When NOHZ is enabled and the tick is stopped, we need to kick the
 * tick timer from irq_enter() so that the jiffies update is kept
 * alive during long running softirqs. That's ugly as hell, but
 * correctness is key even if we need to fix the offending softirq in
 * the first place.
 *
 * Note, this is different to tick_nohz_restart. We just kick the
 * timer and do not touch the other magic bits which need to be done
 * when idle is left.
 */
static void tick_nohz_kick_tick(int cpu, ktime_t now)
{
#if 0
	/* Switch back to 2.6.27 behaviour */

	struct tick_sched *ts = &per_cpu(tick_cpu_sched, cpu);
	ktime_t delta;

	/*
	 * Do not touch the tick device, when the next expiry is either
	 * already reached or less/equal than the tick period.
	 */
	delta =	ktime_sub(hrtimer_get_expires(&ts->sched_timer), now);
	if (delta.tv64 <= tick_period.tv64)
		return;

	tick_nohz_restart(ts, now);
#endif
}

static inline void tick_check_nohz(int cpu)
{
	struct tick_sched *ts = &per_cpu(tick_cpu_sched, cpu);
	ktime_t now;

	if (!ts->idle_active && !ts->tick_stopped)
		return;
	now = ktime_get();
	if (ts->idle_active)
		tick_nohz_stop_idle(cpu, now);
	if (ts->tick_stopped) {
		tick_nohz_update_jiffies(now);
		tick_nohz_kick_tick(cpu, now);
	}
}

#else

static inline void tick_nohz_switch_to_nohz(void) { }
static inline void tick_check_nohz(int cpu) { }

#endif /* NO_HZ */

/*
 * Called from irq_enter to notify about the possible interruption of idle()
 */
void tick_check_idle(int cpu)
{
	tick_check_oneshot_broadcast(cpu);
	tick_check_nohz(cpu);
}

/*
 * High resolution timer specific code
 */
#ifdef CONFIG_HIGH_RES_TIMERS
/*
 * We rearm the timer until we get disabled by the idle code.
 * Called with interrupts disabled and timer->base->cpu_base->lock held.
 */
static enum hrtimer_restart tick_sched_timer(struct hrtimer *timer)
{
	struct tick_sched *ts =
		container_of(timer, struct tick_sched, sched_timer);
	struct pt_regs *regs = get_irq_regs();
	ktime_t now = ktime_get();
	int cpu = smp_processor_id();

#ifdef CONFIG_NO_HZ
	/*
	 * Check if the do_timer duty was dropped. We don't care about
	 * concurrency: This happens only when the cpu in charge went
	 * into a long sleep. If two cpus happen to assign themself to
	 * this duty, then the jiffies update is still serialized by
	 * xtime_lock.
	 */
	if (unlikely(tick_do_timer_cpu == TICK_DO_TIMER_NONE))
		tick_do_timer_cpu = cpu;
#endif

	/* Check, if the jiffies need an update */
	if (tick_do_timer_cpu == cpu)
		tick_do_update_jiffies64(now);

	/*
	 * Do not call, when we are not in irq context and have
	 * no valid regs pointer
	 */
	if (regs) {
		/*
		 * When we are idle and the tick is stopped, we have to touch
		 * the watchdog as we might not schedule for a really long
		 * time. This happens on complete idle SMP systems while
		 * waiting on the login prompt. We also increment the "start of
		 * idle" jiffy stamp so the idle accounting adjustment we do
		 * when we go busy again does not account too much ticks.
		 */
		if (ts->tick_stopped) {
			touch_softlockup_watchdog();
			ts->idle_jiffies++;
		}
		update_process_times(user_mode(regs));
		profile_tick(CPU_PROFILING);
	}

	hrtimer_forward(timer, now, tick_period);

	return HRTIMER_RESTART;
}

/**
 * tick_setup_sched_timer - setup the tick emulation timer
 */
void tick_setup_sched_timer(void)
{
	struct tick_sched *ts = &__get_cpu_var(tick_cpu_sched);
	ktime_t now = ktime_get();

	/*
	 * Emulate tick processing via per-CPU hrtimers:
	 */
	hrtimer_init(&ts->sched_timer, CLOCK_MONOTONIC, HRTIMER_MODE_ABS);
	ts->sched_timer.function = tick_sched_timer;

	/* Get the next period (per cpu) */
	hrtimer_set_expires(&ts->sched_timer, tick_init_jiffy_update());

	for (;;) {
		hrtimer_forward(&ts->sched_timer, now, tick_period);
		hrtimer_start_expires(&ts->sched_timer,
				      HRTIMER_MODE_ABS_PINNED);
		/* Check, if the timer was already in the past */
		if (hrtimer_active(&ts->sched_timer))
			break;
		now = ktime_get();
	}

#ifdef CONFIG_NO_HZ
	if (tick_nohz_enabled) {
		ts->nohz_mode = NOHZ_MODE_HIGHRES;
		printk(KERN_INFO "Switched to NOHz mode on CPU #%d\n", smp_processor_id());
	}
#endif
}
#endif /* HIGH_RES_TIMERS */

#if defined CONFIG_NO_HZ || defined CONFIG_HIGH_RES_TIMERS
void tick_cancel_sched_timer(int cpu)
{
	struct tick_sched *ts = &per_cpu(tick_cpu_sched, cpu);

# ifdef CONFIG_HIGH_RES_TIMERS
	if (ts->sched_timer.base)
		hrtimer_cancel(&ts->sched_timer);
# endif

	memset(ts, 0, sizeof(*ts));
}
#endif

/**
 * Async notification about clocksource changes
 */
void tick_clock_notify(void)
{
	int cpu;

	for_each_possible_cpu(cpu)
		set_bit(0, &per_cpu(tick_cpu_sched, cpu).check_clocks);
}

/*
 * Async notification about clock event changes
 */
void tick_oneshot_notify(void)
{
	struct tick_sched *ts = &__get_cpu_var(tick_cpu_sched);

	set_bit(0, &ts->check_clocks);
}

/**
 * Check, if a change happened, which makes oneshot possible.
 *
 * Called cyclic from the hrtimer softirq (driven by the timer
 * softirq) allow_nohz signals, that we can switch into low-res nohz
 * mode, because high resolution timers are disabled (either compile
 * or runtime).
 */
int tick_check_oneshot_change(int allow_nohz)
{
	struct tick_sched *ts = &__get_cpu_var(tick_cpu_sched);

	if (!test_and_clear_bit(0, &ts->check_clocks))
		return 0;

	if (ts->nohz_mode != NOHZ_MODE_INACTIVE)
		return 0;

	if (!timekeeping_valid_for_hres() || !tick_is_oneshot_available())
		return 0;

	if (!allow_nohz)
		return 1;

	tick_nohz_switch_to_nohz();
	return 0;
}
