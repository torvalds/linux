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
#include <linux/nmi.h>
#include <linux/profile.h>
#include <linux/sched/signal.h>
#include <linux/sched/clock.h>
#include <linux/sched/stat.h>
#include <linux/sched/nohz.h>
#include <linux/module.h>
#include <linux/irq_work.h>
#include <linux/posix-timers.h>
#include <linux/context_tracking.h>

#include <asm/irq_regs.h>

#include "tick-internal.h"

#include <trace/events/timer.h>

/*
 * Per-CPU nohz control structure
 */
static DEFINE_PER_CPU(struct tick_sched, tick_cpu_sched);

struct tick_sched *tick_get_tick_sched(int cpu)
{
	return &per_cpu(tick_cpu_sched, cpu);
}

#if defined(CONFIG_NO_HZ_COMMON) || defined(CONFIG_HIGH_RES_TIMERS)
/*
 * The time, when the last jiffy update happened. Protected by jiffies_lock.
 */
static ktime_t last_jiffies_update;

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
	if (delta < tick_period)
		return;

	/* Reevaluate with jiffies_lock held */
	write_seqlock(&jiffies_lock);

	delta = ktime_sub(now, last_jiffies_update);
	if (delta >= tick_period) {

		delta = ktime_sub(delta, tick_period);
		last_jiffies_update = ktime_add(last_jiffies_update,
						tick_period);

		/* Slow path for long timeouts */
		if (unlikely(delta >= tick_period)) {
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
	if (last_jiffies_update == 0)
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
	 * concurrency: This happens only when the CPU in charge went
	 * into a long sleep. If two CPUs happen to assign themselves to
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
		touch_softlockup_watchdog_sched();
		if (is_idle_task(current))
			ts->idle_jiffies++;
		/*
		 * In case the current tick fired too early past its expected
		 * expiration, make sure we don't bypass the next clock reprogramming
		 * to the same deadline.
		 */
		ts->next_tick = 0;
	}
#endif
	update_process_times(user_mode(regs));
	profile_tick(CPU_PROFILING);
}
#endif

#ifdef CONFIG_NO_HZ_FULL
cpumask_var_t tick_nohz_full_mask;
cpumask_var_t housekeeping_mask;
bool tick_nohz_full_running;
static atomic_t tick_dep_mask;

static bool check_tick_dependency(atomic_t *dep)
{
	int val = atomic_read(dep);

	if (val & TICK_DEP_MASK_POSIX_TIMER) {
		trace_tick_stop(0, TICK_DEP_MASK_POSIX_TIMER);
		return true;
	}

	if (val & TICK_DEP_MASK_PERF_EVENTS) {
		trace_tick_stop(0, TICK_DEP_MASK_PERF_EVENTS);
		return true;
	}

	if (val & TICK_DEP_MASK_SCHED) {
		trace_tick_stop(0, TICK_DEP_MASK_SCHED);
		return true;
	}

	if (val & TICK_DEP_MASK_CLOCK_UNSTABLE) {
		trace_tick_stop(0, TICK_DEP_MASK_CLOCK_UNSTABLE);
		return true;
	}

	return false;
}

static bool can_stop_full_tick(int cpu, struct tick_sched *ts)
{
	WARN_ON_ONCE(!irqs_disabled());

	if (unlikely(!cpu_online(cpu)))
		return false;

	if (check_tick_dependency(&tick_dep_mask))
		return false;

	if (check_tick_dependency(&ts->tick_dep_mask))
		return false;

	if (check_tick_dependency(&current->tick_dep_mask))
		return false;

	if (check_tick_dependency(&current->signal->tick_dep_mask))
		return false;

	return true;
}

static void nohz_full_kick_func(struct irq_work *work)
{
	/* Empty, the tick restart happens on tick_nohz_irq_exit() */
}

static DEFINE_PER_CPU(struct irq_work, nohz_full_kick_work) = {
	.func = nohz_full_kick_func,
};

/*
 * Kick this CPU if it's full dynticks in order to force it to
 * re-evaluate its dependency on the tick and restart it if necessary.
 * This kick, unlike tick_nohz_full_kick_cpu() and tick_nohz_full_kick_all(),
 * is NMI safe.
 */
static void tick_nohz_full_kick(void)
{
	if (!tick_nohz_full_cpu(smp_processor_id()))
		return;

	irq_work_queue(this_cpu_ptr(&nohz_full_kick_work));
}

/*
 * Kick the CPU if it's full dynticks in order to force it to
 * re-evaluate its dependency on the tick and restart it if necessary.
 */
void tick_nohz_full_kick_cpu(int cpu)
{
	if (!tick_nohz_full_cpu(cpu))
		return;

	irq_work_queue_on(&per_cpu(nohz_full_kick_work, cpu), cpu);
}

/*
 * Kick all full dynticks CPUs in order to force these to re-evaluate
 * their dependency on the tick and restart it if necessary.
 */
static void tick_nohz_full_kick_all(void)
{
	int cpu;

	if (!tick_nohz_full_running)
		return;

	preempt_disable();
	for_each_cpu_and(cpu, tick_nohz_full_mask, cpu_online_mask)
		tick_nohz_full_kick_cpu(cpu);
	preempt_enable();
}

static void tick_nohz_dep_set_all(atomic_t *dep,
				  enum tick_dep_bits bit)
{
	int prev;

	prev = atomic_fetch_or(BIT(bit), dep);
	if (!prev)
		tick_nohz_full_kick_all();
}

/*
 * Set a global tick dependency. Used by perf events that rely on freq and
 * by unstable clock.
 */
void tick_nohz_dep_set(enum tick_dep_bits bit)
{
	tick_nohz_dep_set_all(&tick_dep_mask, bit);
}

void tick_nohz_dep_clear(enum tick_dep_bits bit)
{
	atomic_andnot(BIT(bit), &tick_dep_mask);
}

/*
 * Set per-CPU tick dependency. Used by scheduler and perf events in order to
 * manage events throttling.
 */
void tick_nohz_dep_set_cpu(int cpu, enum tick_dep_bits bit)
{
	int prev;
	struct tick_sched *ts;

	ts = per_cpu_ptr(&tick_cpu_sched, cpu);

	prev = atomic_fetch_or(BIT(bit), &ts->tick_dep_mask);
	if (!prev) {
		preempt_disable();
		/* Perf needs local kick that is NMI safe */
		if (cpu == smp_processor_id()) {
			tick_nohz_full_kick();
		} else {
			/* Remote irq work not NMI-safe */
			if (!WARN_ON_ONCE(in_nmi()))
				tick_nohz_full_kick_cpu(cpu);
		}
		preempt_enable();
	}
}

void tick_nohz_dep_clear_cpu(int cpu, enum tick_dep_bits bit)
{
	struct tick_sched *ts = per_cpu_ptr(&tick_cpu_sched, cpu);

	atomic_andnot(BIT(bit), &ts->tick_dep_mask);
}

/*
 * Set a per-task tick dependency. Posix CPU timers need this in order to elapse
 * per task timers.
 */
void tick_nohz_dep_set_task(struct task_struct *tsk, enum tick_dep_bits bit)
{
	/*
	 * We could optimize this with just kicking the target running the task
	 * if that noise matters for nohz full users.
	 */
	tick_nohz_dep_set_all(&tsk->tick_dep_mask, bit);
}

void tick_nohz_dep_clear_task(struct task_struct *tsk, enum tick_dep_bits bit)
{
	atomic_andnot(BIT(bit), &tsk->tick_dep_mask);
}

/*
 * Set a per-taskgroup tick dependency. Posix CPU timers need this in order to elapse
 * per process timers.
 */
void tick_nohz_dep_set_signal(struct signal_struct *sig, enum tick_dep_bits bit)
{
	tick_nohz_dep_set_all(&sig->tick_dep_mask, bit);
}

void tick_nohz_dep_clear_signal(struct signal_struct *sig, enum tick_dep_bits bit)
{
	atomic_andnot(BIT(bit), &sig->tick_dep_mask);
}

/*
 * Re-evaluate the need for the tick as we switch the current task.
 * It might need the tick due to per task/process properties:
 * perf events, posix CPU timers, ...
 */
void __tick_nohz_task_switch(void)
{
	unsigned long flags;
	struct tick_sched *ts;

	local_irq_save(flags);

	if (!tick_nohz_full_cpu(smp_processor_id()))
		goto out;

	ts = this_cpu_ptr(&tick_cpu_sched);

	if (ts->tick_stopped) {
		if (atomic_read(&current->tick_dep_mask) ||
		    atomic_read(&current->signal->tick_dep_mask))
			tick_nohz_full_kick();
	}
out:
	local_irq_restore(flags);
}

/* Parse the boot-time nohz CPU list from the kernel parameters. */
static int __init tick_nohz_full_setup(char *str)
{
	alloc_bootmem_cpumask_var(&tick_nohz_full_mask);
	if (cpulist_parse(str, tick_nohz_full_mask) < 0) {
		pr_warn("NO_HZ: Incorrect nohz_full cpumask\n");
		free_bootmem_cpumask_var(tick_nohz_full_mask);
		return 1;
	}
	tick_nohz_full_running = true;

	return 1;
}
__setup("nohz_full=", tick_nohz_full_setup);

static int tick_nohz_cpu_down(unsigned int cpu)
{
	/*
	 * The boot CPU handles housekeeping duty (unbound timers,
	 * workqueues, timekeeping, ...) on behalf of full dynticks
	 * CPUs. It must remain online when nohz full is enabled.
	 */
	if (tick_nohz_full_running && tick_do_timer_cpu == cpu)
		return -EBUSY;
	return 0;
}

static int tick_nohz_init_all(void)
{
	int err = -1;

#ifdef CONFIG_NO_HZ_FULL_ALL
	if (!alloc_cpumask_var(&tick_nohz_full_mask, GFP_KERNEL)) {
		WARN(1, "NO_HZ: Can't allocate full dynticks cpumask\n");
		return err;
	}
	err = 0;
	cpumask_setall(tick_nohz_full_mask);
	tick_nohz_full_running = true;
#endif
	return err;
}

void __init tick_nohz_init(void)
{
	int cpu, ret;

	if (!tick_nohz_full_running) {
		if (tick_nohz_init_all() < 0)
			return;
	}

	if (!alloc_cpumask_var(&housekeeping_mask, GFP_KERNEL)) {
		WARN(1, "NO_HZ: Can't allocate not-full dynticks cpumask\n");
		cpumask_clear(tick_nohz_full_mask);
		tick_nohz_full_running = false;
		return;
	}

	/*
	 * Full dynticks uses irq work to drive the tick rescheduling on safe
	 * locking contexts. But then we need irq work to raise its own
	 * interrupts to avoid circular dependency on the tick
	 */
	if (!arch_irq_work_has_interrupt()) {
		pr_warn("NO_HZ: Can't run full dynticks because arch doesn't support irq work self-IPIs\n");
		cpumask_clear(tick_nohz_full_mask);
		cpumask_copy(housekeeping_mask, cpu_possible_mask);
		tick_nohz_full_running = false;
		return;
	}

	cpu = smp_processor_id();

	if (cpumask_test_cpu(cpu, tick_nohz_full_mask)) {
		pr_warn("NO_HZ: Clearing %d from nohz_full range for timekeeping\n",
			cpu);
		cpumask_clear_cpu(cpu, tick_nohz_full_mask);
	}

	cpumask_andnot(housekeeping_mask,
		       cpu_possible_mask, tick_nohz_full_mask);

	for_each_cpu(cpu, tick_nohz_full_mask)
		context_tracking_cpu_set(cpu);

	ret = cpuhp_setup_state_nocalls(CPUHP_AP_ONLINE_DYN,
					"kernel/nohz:predown", NULL,
					tick_nohz_cpu_down);
	WARN_ON(ret < 0);
	pr_info("NO_HZ: Full dynticks CPUs: %*pbl.\n",
		cpumask_pr_args(tick_nohz_full_mask));

	/*
	 * We need at least one CPU to handle housekeeping work such
	 * as timekeeping, unbound timers, workqueues, ...
	 */
	WARN_ON_ONCE(cpumask_empty(housekeeping_mask));
}
#endif

/*
 * NOHZ - aka dynamic tick functionality
 */
#ifdef CONFIG_NO_HZ_COMMON
/*
 * NO HZ enabled ?
 */
bool tick_nohz_enabled __read_mostly  = true;
unsigned long tick_nohz_active  __read_mostly;
/*
 * Enable / Disable tickless mode
 */
static int __init setup_tick_nohz(char *str)
{
	return (kstrtobool(str, &tick_nohz_enabled) == 0);
}

__setup("nohz=", setup_tick_nohz);

int tick_nohz_tick_stopped(void)
{
	return __this_cpu_read(tick_cpu_sched.tick_stopped);
}

/**
 * tick_nohz_update_jiffies - update jiffies when idle was interrupted
 *
 * Called from interrupt entry when the CPU was idle
 *
 * In case the sched_tick was stopped on this CPU, we have to check if jiffies
 * must be updated. Otherwise an interrupt handler could use a stale jiffy
 * value. We do this unconditionally on any CPU, as we don't know whether the
 * CPU, which has the update task assigned is in a long sleep.
 */
static void tick_nohz_update_jiffies(ktime_t now)
{
	unsigned long flags;

	__this_cpu_write(tick_cpu_sched.idle_waketime, now);

	local_irq_save(flags);
	tick_do_update_jiffies64(now);
	local_irq_restore(flags);

	touch_softlockup_watchdog_sched();
}

/*
 * Updates the per-CPU time idle statistics counters
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
 * get_cpu_idle_time_us - get the total idle time of a CPU
 * @cpu: CPU number to query
 * @last_update_time: variable to store update time in. Do not update
 * counters if NULL.
 *
 * Return the cumulative idle time (since boot) for a given
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
 * get_cpu_iowait_time_us - get the total iowait time of a CPU
 * @cpu: CPU number to query
 * @last_update_time: variable to store update time in. Do not update
 * counters if NULL.
 *
 * Return the cumulative iowait time (since boot) for a given
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

static void tick_nohz_restart(struct tick_sched *ts, ktime_t now)
{
	hrtimer_cancel(&ts->sched_timer);
	hrtimer_set_expires(&ts->sched_timer, ts->last_tick);

	/* Forward the time to expire in the future */
	hrtimer_forward(&ts->sched_timer, now, tick_period);

	if (ts->nohz_mode == NOHZ_MODE_HIGHRES)
		hrtimer_start_expires(&ts->sched_timer, HRTIMER_MODE_ABS_PINNED);
	else
		tick_program_event(hrtimer_get_expires(&ts->sched_timer), 1);

	/*
	 * Reset to make sure next tick stop doesn't get fooled by past
	 * cached clock deadline.
	 */
	ts->next_tick = 0;
}

static ktime_t tick_nohz_stop_sched_tick(struct tick_sched *ts,
					 ktime_t now, int cpu)
{
	struct clock_event_device *dev = __this_cpu_read(tick_cpu_device.evtdev);
	u64 basemono, next_tick, next_tmr, next_rcu, delta, expires;
	unsigned long seq, basejiff;
	ktime_t	tick;

	/* Read jiffies and the time when jiffies were updated last */
	do {
		seq = read_seqbegin(&jiffies_lock);
		basemono = last_jiffies_update;
		basejiff = jiffies;
	} while (read_seqretry(&jiffies_lock, seq));
	ts->last_jiffies = basejiff;

	if (rcu_needs_cpu(basemono, &next_rcu) ||
	    arch_needs_cpu() || irq_work_needs_cpu()) {
		next_tick = basemono + TICK_NSEC;
	} else {
		/*
		 * Get the next pending timer. If high resolution
		 * timers are enabled this only takes the timer wheel
		 * timers into account. If high resolution timers are
		 * disabled this also looks at the next expiring
		 * hrtimer.
		 */
		next_tmr = get_next_timer_interrupt(basejiff, basemono);
		ts->next_timer = next_tmr;
		/* Take the next rcu event into account */
		next_tick = next_rcu < next_tmr ? next_rcu : next_tmr;
	}

	/*
	 * If the tick is due in the next period, keep it ticking or
	 * force prod the timer.
	 */
	delta = next_tick - basemono;
	if (delta <= (u64)TICK_NSEC) {
		/*
		 * Tell the timer code that the base is not idle, i.e. undo
		 * the effect of get_next_timer_interrupt():
		 */
		timer_clear_idle();
		/*
		 * We've not stopped the tick yet, and there's a timer in the
		 * next period, so no point in stopping it either, bail.
		 */
		if (!ts->tick_stopped) {
			tick = 0;
			goto out;
		}
	}

	/*
	 * If this CPU is the one which updates jiffies, then give up
	 * the assignment and let it be taken by the CPU which runs
	 * the tick timer next, which might be this CPU as well. If we
	 * don't drop this here the jiffies might be stale and
	 * do_timer() never invoked. Keep track of the fact that it
	 * was the one which had the do_timer() duty last. If this CPU
	 * is the one which had the do_timer() duty last, we limit the
	 * sleep time to the timekeeping max_deferment value.
	 * Otherwise we can sleep as long as we want.
	 */
	delta = timekeeping_max_deferment();
	if (cpu == tick_do_timer_cpu) {
		tick_do_timer_cpu = TICK_DO_TIMER_NONE;
		ts->do_timer_last = 1;
	} else if (tick_do_timer_cpu != TICK_DO_TIMER_NONE) {
		delta = KTIME_MAX;
		ts->do_timer_last = 0;
	} else if (!ts->do_timer_last) {
		delta = KTIME_MAX;
	}

#ifdef CONFIG_NO_HZ_FULL
	/* Limit the tick delta to the maximum scheduler deferment */
	if (!ts->inidle)
		delta = min(delta, scheduler_tick_max_deferment());
#endif

	/* Calculate the next expiry time */
	if (delta < (KTIME_MAX - basemono))
		expires = basemono + delta;
	else
		expires = KTIME_MAX;

	expires = min_t(u64, expires, next_tick);
	tick = expires;

	/* Skip reprogram of event if its not changed */
	if (ts->tick_stopped && (expires == ts->next_tick)) {
		/* Sanity check: make sure clockevent is actually programmed */
		if (likely(dev->next_event <= ts->next_tick))
			goto out;

		WARN_ON_ONCE(1);
		printk_once("basemono: %llu ts->next_tick: %llu dev->next_event: %llu timer->active: %d timer->expires: %llu\n",
			    basemono, ts->next_tick, dev->next_event,
			    hrtimer_active(&ts->sched_timer), hrtimer_get_expires(&ts->sched_timer));
	}

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
		cpu_load_update_nohz_start();

		ts->last_tick = hrtimer_get_expires(&ts->sched_timer);
		ts->tick_stopped = 1;
		trace_tick_stop(1, TICK_DEP_MASK_NONE);
	}

	ts->next_tick = tick;

	/*
	 * If the expiration time == KTIME_MAX, then we simply stop
	 * the tick timer.
	 */
	if (unlikely(expires == KTIME_MAX)) {
		if (ts->nohz_mode == NOHZ_MODE_HIGHRES)
			hrtimer_cancel(&ts->sched_timer);
		goto out;
	}

	if (ts->nohz_mode == NOHZ_MODE_HIGHRES)
		hrtimer_start(&ts->sched_timer, tick, HRTIMER_MODE_ABS_PINNED);
	else
		tick_program_event(tick, 1);
out:
	/*
	 * Update the estimated sleep length until the next timer
	 * (not only the tick).
	 */
	ts->sleep_length = ktime_sub(dev->next_event, now);
	return tick;
}

static void tick_nohz_restart_sched_tick(struct tick_sched *ts, ktime_t now)
{
	/* Update jiffies first */
	tick_do_update_jiffies64(now);
	cpu_load_update_nohz_stop();

	/*
	 * Clear the timer idle flag, so we avoid IPIs on remote queueing and
	 * the clock forward checks in the enqueue path:
	 */
	timer_clear_idle();

	calc_load_exit_idle();
	touch_softlockup_watchdog_sched();
	/*
	 * Cancel the scheduled timer and restore the tick
	 */
	ts->tick_stopped  = 0;
	ts->idle_exittime = now;

	tick_nohz_restart(ts, now);
}

static void tick_nohz_full_update_tick(struct tick_sched *ts)
{
#ifdef CONFIG_NO_HZ_FULL
	int cpu = smp_processor_id();

	if (!tick_nohz_full_cpu(cpu))
		return;

	if (!ts->tick_stopped && ts->nohz_mode == NOHZ_MODE_INACTIVE)
		return;

	if (can_stop_full_tick(cpu, ts))
		tick_nohz_stop_sched_tick(ts, ktime_get(), cpu);
	else if (ts->tick_stopped)
		tick_nohz_restart_sched_tick(ts, ktime_get());
#endif
}

static bool can_stop_idle_tick(int cpu, struct tick_sched *ts)
{
	/*
	 * If this CPU is offline and it is the one which updates
	 * jiffies, then give up the assignment and let it be taken by
	 * the CPU which runs the tick timer next. If we don't drop
	 * this here the jiffies might be stale and do_timer() never
	 * invoked.
	 */
	if (unlikely(!cpu_online(cpu))) {
		if (cpu == tick_do_timer_cpu)
			tick_do_timer_cpu = TICK_DO_TIMER_NONE;
		/*
		 * Make sure the CPU doesn't get fooled by obsolete tick
		 * deadline if it comes back online later.
		 */
		ts->next_tick = 0;
		return false;
	}

	if (unlikely(ts->nohz_mode == NOHZ_MODE_INACTIVE)) {
		ts->sleep_length = NSEC_PER_SEC / HZ;
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
		if (expires > 0LL) {
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

	ts = this_cpu_ptr(&tick_cpu_sched);
	ts->inidle = 1;
	__tick_nohz_idle_enter(ts);

	local_irq_enable();
}

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
	struct tick_sched *ts = this_cpu_ptr(&tick_cpu_sched);

	if (ts->inidle)
		__tick_nohz_idle_enter(ts);
	else
		tick_nohz_full_update_tick(ts);
}

/**
 * tick_nohz_get_sleep_length - return the length of the current sleep
 *
 * Called from power state control code with interrupts disabled
 */
ktime_t tick_nohz_get_sleep_length(void)
{
	struct tick_sched *ts = this_cpu_ptr(&tick_cpu_sched);

	return ts->sleep_length;
}

/**
 * tick_nohz_get_idle_calls - return the current idle calls counter value
 *
 * Called from the schedutil frequency scaling governor in scheduler context.
 */
unsigned long tick_nohz_get_idle_calls(void)
{
	struct tick_sched *ts = this_cpu_ptr(&tick_cpu_sched);

	return ts->idle_calls;
}

static void tick_nohz_account_idle_ticks(struct tick_sched *ts)
{
#ifndef CONFIG_VIRT_CPU_ACCOUNTING_NATIVE
	unsigned long ticks;

	if (vtime_accounting_cpu_enabled())
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
	struct tick_sched *ts = this_cpu_ptr(&tick_cpu_sched);
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

/*
 * The nohz low res interrupt handler
 */
static void tick_nohz_handler(struct clock_event_device *dev)
{
	struct tick_sched *ts = this_cpu_ptr(&tick_cpu_sched);
	struct pt_regs *regs = get_irq_regs();
	ktime_t now = ktime_get();

	dev->next_event = KTIME_MAX;

	tick_sched_do_timer(now);
	tick_sched_handle(ts, regs);

	/* No need to reprogram if we are running tickless  */
	if (unlikely(ts->tick_stopped))
		return;

	hrtimer_forward(&ts->sched_timer, now, tick_period);
	tick_program_event(hrtimer_get_expires(&ts->sched_timer), 1);
}

static inline void tick_nohz_activate(struct tick_sched *ts, int mode)
{
	if (!tick_nohz_enabled)
		return;
	ts->nohz_mode = mode;
	/* One update is enough */
	if (!test_and_set_bit(0, &tick_nohz_active))
		timers_update_migration(true);
}

/**
 * tick_nohz_switch_to_nohz - switch to nohz mode
 */
static void tick_nohz_switch_to_nohz(void)
{
	struct tick_sched *ts = this_cpu_ptr(&tick_cpu_sched);
	ktime_t next;

	if (!tick_nohz_enabled)
		return;

	if (tick_switch_to_oneshot(tick_nohz_handler))
		return;

	/*
	 * Recycle the hrtimer in ts, so we can share the
	 * hrtimer_forward with the highres code.
	 */
	hrtimer_init(&ts->sched_timer, CLOCK_MONOTONIC, HRTIMER_MODE_ABS);
	/* Get the next period */
	next = tick_init_jiffy_update();

	hrtimer_set_expires(&ts->sched_timer, next);
	hrtimer_forward_now(&ts->sched_timer, tick_period);
	tick_program_event(hrtimer_get_expires(&ts->sched_timer), 1);
	tick_nohz_activate(ts, NOHZ_MODE_LOWRES);
}

static inline void tick_nohz_irq_enter(void)
{
	struct tick_sched *ts = this_cpu_ptr(&tick_cpu_sched);
	ktime_t now;

	if (!ts->idle_active && !ts->tick_stopped)
		return;
	now = ktime_get();
	if (ts->idle_active)
		tick_nohz_stop_idle(ts, now);
	if (ts->tick_stopped)
		tick_nohz_update_jiffies(now);
}

#else

static inline void tick_nohz_switch_to_nohz(void) { }
static inline void tick_nohz_irq_enter(void) { }
static inline void tick_nohz_activate(struct tick_sched *ts, int mode) { }

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
	else
		ts->next_tick = 0;

	/* No need to reprogram if we are in idle or full dynticks mode */
	if (unlikely(ts->tick_stopped))
		return HRTIMER_NORESTART;

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
	struct tick_sched *ts = this_cpu_ptr(&tick_cpu_sched);
	ktime_t now = ktime_get();

	/*
	 * Emulate tick processing via per-CPU hrtimers:
	 */
	hrtimer_init(&ts->sched_timer, CLOCK_MONOTONIC, HRTIMER_MODE_ABS);
	ts->sched_timer.function = tick_sched_timer;

	/* Get the next period (per-CPU) */
	hrtimer_set_expires(&ts->sched_timer, tick_init_jiffy_update());

	/* Offset the tick to avert jiffies_lock contention. */
	if (sched_skew_tick) {
		u64 offset = ktime_to_ns(tick_period) >> 1;
		do_div(offset, num_possible_cpus());
		offset *= smp_processor_id();
		hrtimer_add_expires_ns(&ts->sched_timer, offset);
	}

	hrtimer_forward(&ts->sched_timer, now, tick_period);
	hrtimer_start_expires(&ts->sched_timer, HRTIMER_MODE_ABS_PINNED);
	tick_nohz_activate(ts, NOHZ_MODE_HIGHRES);
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
	struct tick_sched *ts = this_cpu_ptr(&tick_cpu_sched);

	set_bit(0, &ts->check_clocks);
}

/**
 * Check, if a change happened, which makes oneshot possible.
 *
 * Called cyclic from the hrtimer softirq (driven by the timer
 * softirq) allow_nohz signals, that we can switch into low-res nohz
 * mode, because high resolution timers are disabled (either compile
 * or runtime). Called with interrupts disabled.
 */
int tick_check_oneshot_change(int allow_nohz)
{
	struct tick_sched *ts = this_cpu_ptr(&tick_cpu_sched);

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
