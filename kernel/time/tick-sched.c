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
#include <linux/irq_work.h>
#include <linux/posix-timers.h>
#include <linux/perf_event.h>
#include <linux/context_tracking.h>

#include <asm/irq_regs.h>

#include "tick-internal.h"

#include <trace/events/timer.h>

/*
 * Per cpu nohz control structure
 */
DEFINE_PER_CPU(struct tick_sched, tick_cpu_sched);

/*
 * The time, when the last jiffy update happened. Protected by jiffies_lock.
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
	 * Do a quick check without holding jiffies_lock:
	 */
	delta = ktime_sub(now, last_jiffies_update);
	if (delta.tv64 < tick_period.tv64)
		return;

	/* Reevalute with jiffies_lock held */
	write_seqlock(&jiffies_lock);

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
	} else {
		write_sequnlock(&jiffies_lock);
		return;
	}
	write_sequnlock(&jiffies_lock);
	update_wall_time();
}

/*
 * Initialize and return retrieve the jiffies update.
 */
static ktime_t tick_init_jiffy_update(void)
{
	ktime_t period;

	write_seqlock(&jiffies_lock);
	/* Did we start the jiffies update yet ? */
	if (last_jiffies_update.tv64 == 0)
		last_jiffies_update = tick_next_period;
	period = last_jiffies_update;
	write_sequnlock(&jiffies_lock);
	return period;
}


static void tick_sched_do_timer(ktime_t now)
{
	int cpu = smp_processor_id();

#ifdef CONFIG_NO_HZ_COMMON
	/*
	 * Check if the do_timer duty was dropped. We don't care about
	 * concurrency: This happens only when the cpu in charge went
	 * into a long sleep. If two cpus happen to assign themself to
	 * this duty, then the jiffies update is still serialized by
	 * jiffies_lock.
	 */
	if (unlikely(tick_do_timer_cpu == TICK_DO_TIMER_NONE)
	    && !tick_nohz_full_cpu(cpu))
		tick_do_timer_cpu = cpu;
#endif

	/* Check, if the jiffies need an update */
	if (tick_do_timer_cpu == cpu)
		tick_do_update_jiffies64(now);
}

static void tick_sched_handle(struct tick_sched *ts, struct pt_regs *regs)
{
#ifdef CONFIG_NO_HZ_COMMON
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
		if (is_idle_task(current))
			ts->idle_jiffies++;
	}
#endif
	update_process_times(user_mode(regs));
	profile_tick(CPU_PROFILING);
}

#ifdef CONFIG_NO_HZ_FULL
cpumask_var_t tick_nohz_full_mask;
bool tick_nohz_full_running;

static bool can_stop_full_tick(void)
{
	WARN_ON_ONCE(!irqs_disabled());

	if (!sched_can_stop_tick()) {
		trace_tick_stop(0, "more than 1 task in runqueue\n");
		return false;
	}

	if (!posix_cpu_timers_can_stop_tick(current)) {
		trace_tick_stop(0, "posix timers running\n");
		return false;
	}

	if (!perf_event_can_stop_tick()) {
		trace_tick_stop(0, "perf events running\n");
		return false;
	}

	/* sched_clock_tick() needs us? */
#ifdef CONFIG_HAVE_UNSTABLE_SCHED_CLOCK
	/*
	 * TODO: kick full dynticks CPUs when
	 * sched_clock_stable is set.
	 */
	if (!sched_clock_stable()) {
		trace_tick_stop(0, "unstable sched clock\n");
		/*
		 * Don't allow the user to think they can get
		 * full NO_HZ with this machine.
		 */
		WARN_ONCE(tick_nohz_full_running,
			  "NO_HZ FULL will not work with unstable sched clock");
		return false;
	}
#endif

	return true;
}

static void tick_nohz_restart_sched_tick(struct tick_sched *ts, ktime_t now);

/*
 * Re-evaluate the need for the tick on the current CPU
 * and restart it if necessary.
 */
void __tick_nohz_full_check(void)
{
	struct tick_sched *ts = &__get_cpu_var(tick_cpu_sched);

	if (tick_nohz_full_cpu(smp_processor_id())) {
		if (ts->tick_stopped && !is_idle_task(current)) {
			if (!can_stop_full_tick())
				tick_nohz_restart_sched_tick(ts, ktime_get());
		}
	}
}

static void nohz_full_kick_work_func(struct irq_work *work)
{
	__tick_nohz_full_check();
}

static DEFINE_PER_CPU(struct irq_work, nohz_full_kick_work) = {
	.func = nohz_full_kick_work_func,
};

/*
 * Kick the current CPU if it's full dynticks in order to force it to
 * re-evaluate its dependency on the tick and restart it if necessary.
 */
void tick_nohz_full_kick(void)
{
	if (tick_nohz_full_cpu(smp_processor_id()))
		irq_work_queue(&__get_cpu_var(nohz_full_kick_work));
}

static void nohz_full_kick_ipi(void *info)
{
	__tick_nohz_full_check();
}

/*
 * Kick all full dynticks CPUs in order to force these to re-evaluate
 * their dependency on the tick and restart it if necessary.
 */
void tick_nohz_full_kick_all(void)
{
	if (!tick_nohz_full_running)
		return;

	preempt_disable();
	smp_call_function_many(tick_nohz_full_mask,
			       nohz_full_kick_ipi, NULL, false);
	tick_nohz_full_kick();
	preempt_enable();
}

/*
 * Re-evaluate the need for the tick as we switch the current task.
 * It might need the tick due to per task/process properties:
 * perf events, posix cpu timers, ...
 */
void __tick_nohz_task_switch(struct task_struct *tsk)
{
	unsigned long flags;

	local_irq_save(flags);

	if (!tick_nohz_full_cpu(smp_processor_id()))
		goto out;

	if (tick_nohz_tick_stopped() && !can_stop_full_tick())
		tick_nohz_full_kick();

out:
	local_irq_restore(flags);
}

/* Parse the boot-time nohz CPU list from the kernel parameters. */
static int __init tick_nohz_full_setup(char *str)
{
	int cpu;

	alloc_bootmem_cpumask_var(&tick_nohz_full_mask);
	if (cpulist_parse(str, tick_nohz_full_mask) < 0) {
		pr_warning("NOHZ: Incorrect nohz_full cpumask\n");
		return 1;
	}

	cpu = smp_processor_id();
	if (cpumask_test_cpu(cpu, tick_nohz_full_mask)) {
		pr_warning("NO_HZ: Clearing %d from nohz_full range for timekeeping\n", cpu);
		cpumask_clear_cpu(cpu, tick_nohz_full_mask);
	}
	tick_nohz_full_running = true;

	return 1;
}
__setup("nohz_full=", tick_nohz_full_setup);

static int tick_nohz_cpu_down_callback(struct notifier_block *nfb,
						 unsigned long action,
						 void *hcpu)
{
	unsigned int cpu = (unsigned long)hcpu;

	switch (action & ~CPU_TASKS_FROZEN) {
	case CPU_DOWN_PREPARE:
		/*
		 * If we handle the timekeeping duty for full dynticks CPUs,
		 * we can't safely shutdown that CPU.
		 */
		if (tick_nohz_full_running && tick_do_timer_cpu == cpu)
			return NOTIFY_BAD;
		break;
	}
	return NOTIFY_OK;
}

/*
 * Worst case string length in chunks of CPU range seems 2 steps
 * separations: 0,2,4,6,...
 * This is NR_CPUS + sizeof('\0')
 */
static char __initdata nohz_full_buf[NR_CPUS + 1];

static int tick_nohz_init_all(void)
{
	int err = -1;

#ifdef CONFIG_NO_HZ_FULL_ALL
	if (!alloc_cpumask_var(&tick_nohz_full_mask, GFP_KERNEL)) {
		pr_err("NO_HZ: Can't allocate full dynticks cpumask\n");
		return err;
	}
	err = 0;
	cpumask_setall(tick_nohz_full_mask);
	cpumask_clear_cpu(smp_processor_id(), tick_nohz_full_mask);
	tick_nohz_full_running = true;
#endif
	return err;
}

void __init tick_nohz_init(void)
{
	int cpu;

	if (!tick_nohz_full_running) {
		if (tick_nohz_init_all() < 0)
			return;
	}

	for_each_cpu(cpu, tick_nohz_full_mask)
		context_tracking_cpu_set(cpu);

	cpu_notifier(tick_nohz_cpu_down_callback, 0);
	cpulist_scnprintf(nohz_full_buf, sizeof(nohz_full_buf), tick_nohz_full_mask);
	pr_info("NO_HZ: Full dynticks CPUs: %s.\n", nohz_full_buf);
}
#endif

/*
 * NOHZ - aka dynamic tick functionality
 */
#ifdef CONFIG_NO_HZ_COMMON
/*
 * NO HZ enabled ?
 */
static int tick_nohz_enabled __read_mostly  = 1;
int tick_nohz_active  __read_mostly;
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
	unsigned long flags;

	__this_cpu_write(tick_cpu_sched.idle_waketime, now);

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
		if (nr_iowait_cpu(cpu) > 0)
			ts->iowait_sleeptime = ktime_add(ts->iowait_sleeptime, delta);
		else
			ts->idle_sleeptime = ktime_add(ts->idle_sleeptime, delta);
		ts->idle_entrytime = now;
	}

	if (last_update_time)
		*last_update_time = ktime_to_us(now);

}

static void tick_nohz_stop_idle(struct tick_sched *ts, ktime_t now)
{
	update_ts_time_stats(smp_processor_id(), ts, now, NULL);
	ts->idle_active = 0;

	sched_clock_idle_wakeup_event(0);
}

static ktime_t tick_nohz_start_idle(struct tick_sched *ts)
{
	ktime_t now = ktime_get();

	ts->idle_entrytime = now;
	ts->idle_active = 1;
	sched_clock_idle_sleep_event();
	return now;
}

/**
 * get_cpu_idle_time_us - get the total idle time of a cpu
 * @cpu: CPU number to query
 * @last_update_time: variable to store update time in. Do not update
 * counters if NULL.
 *
 * Return the cummulative idle time (since boot) for a given
 * CPU, in microseconds.
 *
 * This time is measured via accounting rather than sampling,
 * and is as accurate as ktime_get() is.
 *
 * This function returns -1 if NOHZ is not enabled.
 */
u64 get_cpu_idle_time_us(int cpu, u64 *last_update_time)
{
	struct tick_sched *ts = &per_cpu(tick_cpu_sched, cpu);
	ktime_t now, idle;

	if (!tick_nohz_active)
		return -1;

	now = ktime_get();
	if (last_update_time) {
		update_ts_time_stats(cpu, ts, now, last_update_time);
		idle = ts->idle_sleeptime;
	} else {
		if (ts->idle_active && !nr_iowait_cpu(cpu)) {
			ktime_t delta = ktime_sub(now, ts->idle_entrytime);

			idle = ktime_add(ts->idle_sleeptime, delta);
		} else {
			idle = ts->idle_sleeptime;
		}
	}

	return ktime_to_us(idle);

}
EXPORT_SYMBOL_GPL(get_cpu_idle_time_us);

/**
 * get_cpu_iowait_time_us - get the total iowait time of a cpu
 * @cpu: CPU number to query
 * @last_update_time: variable to store update time in. Do not update
 * counters if NULL.
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
	ktime_t now, iowait;

	if (!tick_nohz_active)
		return -1;

	now = ktime_get();
	if (last_update_time) {
		update_ts_time_stats(cpu, ts, now, last_update_time);
		iowait = ts->iowait_sleeptime;
	} else {
		if (ts->idle_active && nr_iowait_cpu(cpu) > 0) {
			ktime_t delta = ktime_sub(now, ts->idle_entrytime);

			iowait = ktime_add(ts->iowait_sleeptime, delta);
		} else {
			iowait = ts->iowait_sleeptime;
		}
	}

	return ktime_to_us(iowait);
}
EXPORT_SYMBOL_GPL(get_cpu_iowait_time_us);

static ktime_t tick_nohz_stop_sched_tick(struct tick_sched *ts,
					 ktime_t now, int cpu)
{
	unsigned long seq, last_jiffies, next_jiffies, delta_jiffies;
	ktime_t last_update, expires, ret = { .tv64 = 0 };
	unsigned long rcu_delta_jiffies;
	struct clock_event_device *dev = __get_cpu_var(tick_cpu_device).evtdev;
	u64 time_delta;

	time_delta = timekeeping_max_deferment();

	/* Read jiffies and the time when jiffies were updated last */
	do {
		seq = read_seqbegin(&jiffies_lock);
		last_update = last_jiffies_update;
		last_jiffies = jiffies;
	} while (read_seqretry(&jiffies_lock, seq));

	if (rcu_needs_cpu(cpu, &rcu_delta_jiffies) ||
	    arch_needs_cpu(cpu) || irq_work_needs_cpu()) {
		next_jiffies = last_jiffies + 1;
		delta_jiffies = 1;
	} else {
		/* Get the next timer wheel timer */
		next_jiffies = get_next_timer_interrupt(last_jiffies);
		delta_jiffies = next_jiffies - last_jiffies;
		if (rcu_delta_jiffies < delta_jiffies) {
			next_jiffies = last_jiffies + rcu_delta_jiffies;
			delta_jiffies = rcu_delta_jiffies;
		}
	}

	/*
	 * Do not stop the tick, if we are only one off (or less)
	 * or if the cpu is required for RCU:
	 */
	if (!ts->tick_stopped && delta_jiffies <= 1)
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

#ifdef CONFIG_NO_HZ_FULL
		if (!ts->inidle) {
			time_delta = min(time_delta,
					 scheduler_tick_max_deferment());
		}
#endif

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

		/* Skip reprogram of event if its not changed */
		if (ts->tick_stopped && ktime_equal(expires, dev->next_event))
			goto out;

		ret = expires;

		/*
		 * nohz_stop_sched_tick can be called several times before
		 * the nohz_restart_sched_tick is called. This happens when
		 * interrupts arrive which do not cause a reschedule. In the
		 * first call we save the current tick time, so we can restart
		 * the scheduler tick in nohz_restart_sched_tick.
		 */
		if (!ts->tick_stopped) {
			nohz_balance_enter_idle(cpu);
			calc_load_enter_idle();

			ts->last_tick = hrtimer_get_expires(&ts->sched_timer);
			ts->tick_stopped = 1;
			trace_tick_stop(1, " ");
		}

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
	}
	raise_softirq_irqoff(TIMER_SOFTIRQ);
out:
	ts->next_jiffies = next_jiffies;
	ts->last_jiffies = last_jiffies;
	ts->sleep_length = ktime_sub(dev->next_event, now);

	return ret;
}

static void tick_nohz_full_stop_tick(struct tick_sched *ts)
{
#ifdef CONFIG_NO_HZ_FULL
	int cpu = smp_processor_id();

	if (!tick_nohz_full_cpu(cpu) || is_idle_task(current))
		return;

	if (!ts->tick_stopped && ts->nohz_mode == NOHZ_MODE_INACTIVE)
		return;

	if (!can_stop_full_tick())
		return;

	tick_nohz_stop_sched_tick(ts, ktime_get(), cpu);
#endif
}

static bool can_stop_idle_tick(int cpu, struct tick_sched *ts)
{
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
		return false;
	}

	if (unlikely(ts->nohz_mode == NOHZ_MODE_INACTIVE)) {
		ts->sleep_length = (ktime_t) { .tv64 = NSEC_PER_SEC/HZ };
		return false;
	}

	if (need_resched())
		return false;

	if (unlikely(local_softirq_pending() && cpu_online(cpu))) {
		static int ratelimit;

		if (ratelimit < 10 &&
		    (local_softirq_pending() & SOFTIRQ_STOP_IDLE_MASK)) {
			pr_warn("NOHZ: local_softirq_pending %02x\n",
				(unsigned int) local_softirq_pending());
			ratelimit++;
		}
		return false;
	}

	if (tick_nohz_full_enabled()) {
		/*
		 * Keep the tick alive to guarantee timekeeping progression
		 * if there are full dynticks CPUs around
		 */
		if (tick_do_timer_cpu == cpu)
			return false;
		/*
		 * Boot safety: make sure the timekeeping duty has been
		 * assigned before entering dyntick-idle mode,
		 */
		if (tick_do_timer_cpu == TICK_DO_TIMER_NONE)
			return false;
	}

	return true;
}

static void __tick_nohz_idle_enter(struct tick_sched *ts)
{
	ktime_t now, expires;
	int cpu = smp_processor_id();

	now = tick_nohz_start_idle(ts);

	if (can_stop_idle_tick(cpu, ts)) {
		int was_stopped = ts->tick_stopped;

		ts->idle_calls++;

		expires = tick_nohz_stop_sched_tick(ts, now, cpu);
		if (expires.tv64 > 0LL) {
			ts->idle_sleeps++;
			ts->idle_expires = expires;
		}

		if (!was_stopped && ts->tick_stopped)
			ts->idle_jiffies = ts->last_jiffies;
	}
}

/**
 * tick_nohz_idle_enter - stop the idle tick from the idle task
 *
 * When the next event is more than a tick into the future, stop the idle tick
 * Called when we start the idle loop.
 *
 * The arch is responsible of calling:
 *
 * - rcu_idle_enter() after its last use of RCU before the CPU is put
 *  to sleep.
 * - rcu_idle_exit() before the first use of RCU after the CPU is woken up.
 */
void tick_nohz_idle_enter(void)
{
	struct tick_sched *ts;

	WARN_ON_ONCE(irqs_disabled());

	/*
 	 * Update the idle state in the scheduler domain hierarchy
 	 * when tick_nohz_stop_sched_tick() is called from the idle loop.
 	 * State will be updated to busy during the first busy tick after
 	 * exiting idle.
 	 */
	set_cpu_sd_state_idle();

	local_irq_disable();

	ts = &__get_cpu_var(tick_cpu_sched);
	ts->inidle = 1;
	__tick_nohz_idle_enter(ts);

	local_irq_enable();
}
EXPORT_SYMBOL_GPL(tick_nohz_idle_enter);

/**
 * tick_nohz_irq_exit - update next tick event from interrupt exit
 *
 * When an interrupt fires while we are idle and it doesn't cause
 * a reschedule, it may still add, modify or delete a timer, enqueue
 * an RCU callback, etc...
 * So we need to re-calculate and reprogram the next tick event.
 */
void tick_nohz_irq_exit(void)
{
	struct tick_sched *ts = &__get_cpu_var(tick_cpu_sched);

	if (ts->inidle)
		__tick_nohz_idle_enter(ts);
	else
		tick_nohz_full_stop_tick(ts);
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
	hrtimer_set_expires(&ts->sched_timer, ts->last_tick);

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

static void tick_nohz_restart_sched_tick(struct tick_sched *ts, ktime_t now)
{
	/* Update jiffies first */
	tick_do_update_jiffies64(now);
	update_cpu_load_nohz();

	calc_load_exit_idle();
	touch_softlockup_watchdog();
	/*
	 * Cancel the scheduled timer and restore the tick
	 */
	ts->tick_stopped  = 0;
	ts->idle_exittime = now;

	tick_nohz_restart(ts, now);
}

static void tick_nohz_account_idle_ticks(struct tick_sched *ts)
{
#ifndef CONFIG_VIRT_CPU_ACCOUNTING_NATIVE
	unsigned long ticks;

	if (vtime_accounting_enabled())
		return;
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
}

/**
 * tick_nohz_idle_exit - restart the idle tick from the idle task
 *
 * Restart the idle tick when the CPU is woken up from idle
 * This also exit the RCU extended quiescent state. The CPU
 * can use RCU again after this function is called.
 */
void tick_nohz_idle_exit(void)
{
	struct tick_sched *ts = &__get_cpu_var(tick_cpu_sched);
	ktime_t now;

	local_irq_disable();

	WARN_ON_ONCE(!ts->inidle);

	ts->inidle = 0;

	if (ts->idle_active || ts->tick_stopped)
		now = ktime_get();

	if (ts->idle_active)
		tick_nohz_stop_idle(ts, now);

	if (ts->tick_stopped) {
		tick_nohz_restart_sched_tick(ts, now);
		tick_nohz_account_idle_ticks(ts);
	}

	local_irq_enable();
}
EXPORT_SYMBOL_GPL(tick_nohz_idle_exit);

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
	ktime_t now = ktime_get();

	dev->next_event.tv64 = KTIME_MAX;

	tick_sched_do_timer(now);
	tick_sched_handle(ts, regs);

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
	tick_nohz_active = 1;
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
static void tick_nohz_kick_tick(struct tick_sched *ts, ktime_t now)
{
#if 0
	/* Switch back to 2.6.27 behaviour */
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

static inline void tick_nohz_irq_enter(void)
{
	struct tick_sched *ts = &__get_cpu_var(tick_cpu_sched);
	ktime_t now;

	if (!ts->idle_active && !ts->tick_stopped)
		return;
	now = ktime_get();
	if (ts->idle_active)
		tick_nohz_stop_idle(ts, now);
	if (ts->tick_stopped) {
		tick_nohz_update_jiffies(now);
		tick_nohz_kick_tick(ts, now);
	}
}

#else

static inline void tick_nohz_switch_to_nohz(void) { }
static inline void tick_nohz_irq_enter(void) { }

#endif /* CONFIG_NO_HZ_COMMON */

/*
 * Called from irq_enter to notify about the possible interruption of idle()
 */
void tick_irq_enter(void)
{
	tick_check_oneshot_broadcast_this_cpu();
	tick_nohz_irq_enter();
}

/*
 * High resolution timer specific code
 */
#ifdef CONFIG_HIGH_RES_TIMERS
/*
 * We rearm the timer until we get disabled by the idle code.
 * Called with interrupts disabled.
 */
static enum hrtimer_restart tick_sched_timer(struct hrtimer *timer)
{
	struct tick_sched *ts =
		container_of(timer, struct tick_sched, sched_timer);
	struct pt_regs *regs = get_irq_regs();
	ktime_t now = ktime_get();

	tick_sched_do_timer(now);

	/*
	 * Do not call, when we are not in irq context and have
	 * no valid regs pointer
	 */
	if (regs)
		tick_sched_handle(ts, regs);

	hrtimer_forward(timer, now, tick_period);

	return HRTIMER_RESTART;
}

static int sched_skew_tick;

static int __init skew_tick(char *str)
{
	get_option(&str, &sched_skew_tick);

	return 0;
}
early_param("skew_tick", skew_tick);

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

	/* Offset the tick to avert jiffies_lock contention. */
	if (sched_skew_tick) {
		u64 offset = ktime_to_ns(tick_period) >> 1;
		do_div(offset, num_possible_cpus());
		offset *= smp_processor_id();
		hrtimer_add_expires_ns(&ts->sched_timer, offset);
	}

	for (;;) {
		hrtimer_forward(&ts->sched_timer, now, tick_period);
		hrtimer_start_expires(&ts->sched_timer,
				      HRTIMER_MODE_ABS_PINNED);
		/* Check, if the timer was already in the past */
		if (hrtimer_active(&ts->sched_timer))
			break;
		now = ktime_get();
	}

#ifdef CONFIG_NO_HZ_COMMON
	if (tick_nohz_enabled) {
		ts->nohz_mode = NOHZ_MODE_HIGHRES;
		tick_nohz_active = 1;
	}
#endif
}
#endif /* HIGH_RES_TIMERS */

#if defined CONFIG_NO_HZ_COMMON || defined CONFIG_HIGH_RES_TIMERS
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
