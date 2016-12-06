/*
 * Copyright (c) 2016, The Linux Foundation. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 and
 * only version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 *
 * Window Assisted Load Tracking (WALT) implementation credits:
 * Srivatsa Vaddagiri, Steve Muckle, Syed Rameez Mustafa, Joonwoo Park,
 * Pavan Kumar Kondeti, Olav Haugan
 *
 * 2016-03-06: Integration with EAS/refactoring by Vikram Mulukutla
 *             and Todd Kjos
 */

#include <linux/syscore_ops.h>
#include <linux/cpufreq.h>
#include <trace/events/sched.h>
#include "sched.h"
#include "walt.h"

#define WINDOW_STATS_RECENT		0
#define WINDOW_STATS_MAX		1
#define WINDOW_STATS_MAX_RECENT_AVG	2
#define WINDOW_STATS_AVG		3
#define WINDOW_STATS_INVALID_POLICY	4

#define EXITING_TASK_MARKER	0xdeaddead

static __read_mostly unsigned int walt_ravg_hist_size = 5;
static __read_mostly unsigned int walt_window_stats_policy =
	WINDOW_STATS_MAX_RECENT_AVG;
static __read_mostly unsigned int walt_account_wait_time = 1;
static __read_mostly unsigned int walt_freq_account_wait_time = 0;
static __read_mostly unsigned int walt_io_is_busy = 0;

unsigned int sysctl_sched_walt_init_task_load_pct = 15;

/* 1 -> use PELT based load stats, 0 -> use window-based load stats */
unsigned int __read_mostly walt_disabled = 0;

static unsigned int max_possible_efficiency = 1024;
static unsigned int min_possible_efficiency = 1024;

/*
 * Maximum possible frequency across all cpus. Task demand and cpu
 * capacity (cpu_power) metrics are scaled in reference to it.
 */
static unsigned int max_possible_freq = 1;

/*
 * Minimum possible max_freq across all cpus. This will be same as
 * max_possible_freq on homogeneous systems and could be different from
 * max_possible_freq on heterogenous systems. min_max_freq is used to derive
 * capacity (cpu_power) of cpus.
 */
static unsigned int min_max_freq = 1;

static unsigned int max_load_scale_factor = 1024;
static unsigned int max_possible_capacity = 1024;

/* Mask of all CPUs that have  max_possible_capacity */
static cpumask_t mpc_mask = CPU_MASK_ALL;

/* Window size (in ns) */
__read_mostly unsigned int walt_ravg_window = 20000000;

/* Min window size (in ns) = 10ms */
#define MIN_SCHED_RAVG_WINDOW 10000000

/* Max window size (in ns) = 1s */
#define MAX_SCHED_RAVG_WINDOW 1000000000

static unsigned int sync_cpu;
static ktime_t ktime_last;
static bool walt_ktime_suspended;

static unsigned int task_load(struct task_struct *p)
{
	return p->ravg.demand;
}

void
walt_inc_cumulative_runnable_avg(struct rq *rq,
				 struct task_struct *p)
{
	rq->cumulative_runnable_avg += p->ravg.demand;
}

void
walt_dec_cumulative_runnable_avg(struct rq *rq,
				 struct task_struct *p)
{
	rq->cumulative_runnable_avg -= p->ravg.demand;
	BUG_ON((s64)rq->cumulative_runnable_avg < 0);
}

static void
fixup_cumulative_runnable_avg(struct rq *rq,
			      struct task_struct *p, s64 task_load_delta)
{
	rq->cumulative_runnable_avg += task_load_delta;
	if ((s64)rq->cumulative_runnable_avg < 0)
		panic("cra less than zero: tld: %lld, task_load(p) = %u\n",
			task_load_delta, task_load(p));
}

u64 walt_ktime_clock(void)
{
	if (unlikely(walt_ktime_suspended))
		return ktime_to_ns(ktime_last);
	return ktime_get_ns();
}

static void walt_resume(void)
{
	walt_ktime_suspended = false;
}

static int walt_suspend(void)
{
	ktime_last = ktime_get();
	walt_ktime_suspended = true;
	return 0;
}

static struct syscore_ops walt_syscore_ops = {
	.resume	= walt_resume,
	.suspend = walt_suspend
};

static int __init walt_init_ops(void)
{
	register_syscore_ops(&walt_syscore_ops);
	return 0;
}
late_initcall(walt_init_ops);

void walt_inc_cfs_cumulative_runnable_avg(struct cfs_rq *cfs_rq,
		struct task_struct *p)
{
	cfs_rq->cumulative_runnable_avg += p->ravg.demand;
}

void walt_dec_cfs_cumulative_runnable_avg(struct cfs_rq *cfs_rq,
		struct task_struct *p)
{
	cfs_rq->cumulative_runnable_avg -= p->ravg.demand;
}

static int exiting_task(struct task_struct *p)
{
	if (p->flags & PF_EXITING) {
		if (p->ravg.sum_history[0] != EXITING_TASK_MARKER) {
			p->ravg.sum_history[0] = EXITING_TASK_MARKER;
		}
		return 1;
	}
	return 0;
}

static int __init set_walt_ravg_window(char *str)
{
	get_option(&str, &walt_ravg_window);

	walt_disabled = (walt_ravg_window < MIN_SCHED_RAVG_WINDOW ||
				walt_ravg_window > MAX_SCHED_RAVG_WINDOW);
	return 0;
}

early_param("walt_ravg_window", set_walt_ravg_window);

static void
update_window_start(struct rq *rq, u64 wallclock)
{
	s64 delta;
	int nr_windows;

	delta = wallclock - rq->window_start;
	/* If the MPM global timer is cleared, set delta as 0 to avoid kernel BUG happening */
	if (delta < 0) {
		delta = 0;
		WARN_ONCE(1, "WALT wallclock appears to have gone backwards or reset\n");
	}

	if (delta < walt_ravg_window)
		return;

	nr_windows = div64_u64(delta, walt_ravg_window);
	rq->window_start += (u64)nr_windows * (u64)walt_ravg_window;
}

static u64 scale_exec_time(u64 delta, struct rq *rq)
{
	unsigned int cur_freq = rq->cur_freq;
	int sf;

	if (unlikely(cur_freq > max_possible_freq))
		cur_freq = rq->max_possible_freq;

	/* round up div64 */
	delta = div64_u64(delta * cur_freq + max_possible_freq - 1,
			  max_possible_freq);

	sf = DIV_ROUND_UP(rq->efficiency * 1024, max_possible_efficiency);

	delta *= sf;
	delta >>= 10;

	return delta;
}

static int cpu_is_waiting_on_io(struct rq *rq)
{
	if (!walt_io_is_busy)
		return 0;

	return atomic_read(&rq->nr_iowait);
}

void walt_account_irqtime(int cpu, struct task_struct *curr,
				 u64 delta, u64 wallclock)
{
	struct rq *rq = cpu_rq(cpu);
	unsigned long flags, nr_windows;
	u64 cur_jiffies_ts;

	raw_spin_lock_irqsave(&rq->lock, flags);

	/*
	 * cputime (wallclock) uses sched_clock so use the same here for
	 * consistency.
	 */
	delta += sched_clock() - wallclock;
	cur_jiffies_ts = get_jiffies_64();

	if (is_idle_task(curr))
		walt_update_task_ravg(curr, rq, IRQ_UPDATE, walt_ktime_clock(),
				 delta);

	nr_windows = cur_jiffies_ts - rq->irqload_ts;

	if (nr_windows) {
		if (nr_windows < 10) {
			/* Decay CPU's irqload by 3/4 for each window. */
			rq->avg_irqload *= (3 * nr_windows);
			rq->avg_irqload = div64_u64(rq->avg_irqload,
						    4 * nr_windows);
		} else {
			rq->avg_irqload = 0;
		}
		rq->avg_irqload += rq->cur_irqload;
		rq->cur_irqload = 0;
	}

	rq->cur_irqload += delta;
	rq->irqload_ts = cur_jiffies_ts;
	raw_spin_unlock_irqrestore(&rq->lock, flags);
}


#define WALT_HIGH_IRQ_TIMEOUT 3

u64 walt_irqload(int cpu) {
	struct rq *rq = cpu_rq(cpu);
	s64 delta;
	delta = get_jiffies_64() - rq->irqload_ts;

        /*
	 * Current context can be preempted by irq and rq->irqload_ts can be
	 * updated by irq context so that delta can be negative.
	 * But this is okay and we can safely return as this means there
	 * was recent irq occurrence.
	 */

        if (delta < WALT_HIGH_IRQ_TIMEOUT)
		return rq->avg_irqload;
        else
		return 0;
}

int walt_cpu_high_irqload(int cpu) {
	return walt_irqload(cpu) >= sysctl_sched_walt_cpu_high_irqload;
}

static int account_busy_for_cpu_time(struct rq *rq, struct task_struct *p,
				     u64 irqtime, int event)
{
	if (is_idle_task(p)) {
		/* TASK_WAKE && TASK_MIGRATE is not possible on idle task! */
		if (event == PICK_NEXT_TASK)
			return 0;

		/* PUT_PREV_TASK, TASK_UPDATE && IRQ_UPDATE are left */
		return irqtime || cpu_is_waiting_on_io(rq);
	}

	if (event == TASK_WAKE)
		return 0;

	if (event == PUT_PREV_TASK || event == IRQ_UPDATE ||
					 event == TASK_UPDATE)
		return 1;

	/* Only TASK_MIGRATE && PICK_NEXT_TASK left */
	return walt_freq_account_wait_time;
}

/*
 * Account cpu activity in its busy time counters (rq->curr/prev_runnable_sum)
 */
static void update_cpu_busy_time(struct task_struct *p, struct rq *rq,
	     int event, u64 wallclock, u64 irqtime)
{
	int new_window, nr_full_windows = 0;
	int p_is_curr_task = (p == rq->curr);
	u64 mark_start = p->ravg.mark_start;
	u64 window_start = rq->window_start;
	u32 window_size = walt_ravg_window;
	u64 delta;

	new_window = mark_start < window_start;
	if (new_window) {
		nr_full_windows = div64_u64((window_start - mark_start),
						window_size);
		if (p->ravg.active_windows < USHRT_MAX)
			p->ravg.active_windows++;
	}

	/* Handle per-task window rollover. We don't care about the idle
	 * task or exiting tasks. */
	if (new_window && !is_idle_task(p) && !exiting_task(p)) {
		u32 curr_window = 0;

		if (!nr_full_windows)
			curr_window = p->ravg.curr_window;

		p->ravg.prev_window = curr_window;
		p->ravg.curr_window = 0;
	}

	if (!account_busy_for_cpu_time(rq, p, irqtime, event)) {
		/* account_busy_for_cpu_time() = 0, so no update to the
		 * task's current window needs to be made. This could be
		 * for example
		 *
		 *   - a wakeup event on a task within the current
		 *     window (!new_window below, no action required),
		 *   - switching to a new task from idle (PICK_NEXT_TASK)
		 *     in a new window where irqtime is 0 and we aren't
		 *     waiting on IO */

		if (!new_window)
			return;

		/* A new window has started. The RQ demand must be rolled
		 * over if p is the current task. */
		if (p_is_curr_task) {
			u64 prev_sum = 0;

			/* p is either idle task or an exiting task */
			if (!nr_full_windows) {
				prev_sum = rq->curr_runnable_sum;
			}

			rq->prev_runnable_sum = prev_sum;
			rq->curr_runnable_sum = 0;
		}

		return;
	}

	if (!new_window) {
		/* account_busy_for_cpu_time() = 1 so busy time needs
		 * to be accounted to the current window. No rollover
		 * since we didn't start a new window. An example of this is
		 * when a task starts execution and then sleeps within the
		 * same window. */

		if (!irqtime || !is_idle_task(p) || cpu_is_waiting_on_io(rq))
			delta = wallclock - mark_start;
		else
			delta = irqtime;
		delta = scale_exec_time(delta, rq);
		rq->curr_runnable_sum += delta;
		if (!is_idle_task(p) && !exiting_task(p))
			p->ravg.curr_window += delta;

		return;
	}

	if (!p_is_curr_task) {
		/* account_busy_for_cpu_time() = 1 so busy time needs
		 * to be accounted to the current window. A new window
		 * has also started, but p is not the current task, so the
		 * window is not rolled over - just split up and account
		 * as necessary into curr and prev. The window is only
		 * rolled over when a new window is processed for the current
		 * task.
		 *
		 * Irqtime can't be accounted by a task that isn't the
		 * currently running task. */

		if (!nr_full_windows) {
			/* A full window hasn't elapsed, account partial
			 * contribution to previous completed window. */
			delta = scale_exec_time(window_start - mark_start, rq);
			if (!exiting_task(p))
				p->ravg.prev_window += delta;
		} else {
			/* Since at least one full window has elapsed,
			 * the contribution to the previous window is the
			 * full window (window_size). */
			delta = scale_exec_time(window_size, rq);
			if (!exiting_task(p))
				p->ravg.prev_window = delta;
		}
		rq->prev_runnable_sum += delta;

		/* Account piece of busy time in the current window. */
		delta = scale_exec_time(wallclock - window_start, rq);
		rq->curr_runnable_sum += delta;
		if (!exiting_task(p))
			p->ravg.curr_window = delta;

		return;
	}

	if (!irqtime || !is_idle_task(p) || cpu_is_waiting_on_io(rq)) {
		/* account_busy_for_cpu_time() = 1 so busy time needs
		 * to be accounted to the current window. A new window
		 * has started and p is the current task so rollover is
		 * needed. If any of these three above conditions are true
		 * then this busy time can't be accounted as irqtime.
		 *
		 * Busy time for the idle task or exiting tasks need not
		 * be accounted.
		 *
		 * An example of this would be a task that starts execution
		 * and then sleeps once a new window has begun. */

		if (!nr_full_windows) {
			/* A full window hasn't elapsed, account partial
			 * contribution to previous completed window. */
			delta = scale_exec_time(window_start - mark_start, rq);
			if (!is_idle_task(p) && !exiting_task(p))
				p->ravg.prev_window += delta;

			delta += rq->curr_runnable_sum;
		} else {
			/* Since at least one full window has elapsed,
			 * the contribution to the previous window is the
			 * full window (window_size). */
			delta = scale_exec_time(window_size, rq);
			if (!is_idle_task(p) && !exiting_task(p))
				p->ravg.prev_window = delta;

		}
		/*
		 * Rollover for normal runnable sum is done here by overwriting
		 * the values in prev_runnable_sum and curr_runnable_sum.
		 * Rollover for new task runnable sum has completed by previous
		 * if-else statement.
		 */
		rq->prev_runnable_sum = delta;

		/* Account piece of busy time in the current window. */
		delta = scale_exec_time(wallclock - window_start, rq);
		rq->curr_runnable_sum = delta;
		if (!is_idle_task(p) && !exiting_task(p))
			p->ravg.curr_window = delta;

		return;
	}

	if (irqtime) {
		/* account_busy_for_cpu_time() = 1 so busy time needs
		 * to be accounted to the current window. A new window
		 * has started and p is the current task so rollover is
		 * needed. The current task must be the idle task because
		 * irqtime is not accounted for any other task.
		 *
		 * Irqtime will be accounted each time we process IRQ activity
		 * after a period of idleness, so we know the IRQ busy time
		 * started at wallclock - irqtime. */

		BUG_ON(!is_idle_task(p));
		mark_start = wallclock - irqtime;

		/* Roll window over. If IRQ busy time was just in the current
		 * window then that is all that need be accounted. */
		rq->prev_runnable_sum = rq->curr_runnable_sum;
		if (mark_start > window_start) {
			rq->curr_runnable_sum = scale_exec_time(irqtime, rq);
			return;
		}

		/* The IRQ busy time spanned multiple windows. Process the
		 * busy time preceding the current window start first. */
		delta = window_start - mark_start;
		if (delta > window_size)
			delta = window_size;
		delta = scale_exec_time(delta, rq);
		rq->prev_runnable_sum += delta;

		/* Process the remaining IRQ busy time in the current window. */
		delta = wallclock - window_start;
		rq->curr_runnable_sum = scale_exec_time(delta, rq);

		return;
	}

	BUG();
}

static int account_busy_for_task_demand(struct task_struct *p, int event)
{
	/* No need to bother updating task demand for exiting tasks
	 * or the idle task. */
	if (exiting_task(p) || is_idle_task(p))
		return 0;

	/* When a task is waking up it is completing a segment of non-busy
	 * time. Likewise, if wait time is not treated as busy time, then
	 * when a task begins to run or is migrated, it is not running and
	 * is completing a segment of non-busy time. */
	if (event == TASK_WAKE || (!walt_account_wait_time &&
			 (event == PICK_NEXT_TASK || event == TASK_MIGRATE)))
		return 0;

	return 1;
}

/*
 * Called when new window is starting for a task, to record cpu usage over
 * recently concluded window(s). Normally 'samples' should be 1. It can be > 1
 * when, say, a real-time task runs without preemption for several windows at a
 * stretch.
 */
static void update_history(struct rq *rq, struct task_struct *p,
			 u32 runtime, int samples, int event)
{
	u32 *hist = &p->ravg.sum_history[0];
	int ridx, widx;
	u32 max = 0, avg, demand;
	u64 sum = 0;

	/* Ignore windows where task had no activity */
	if (!runtime || is_idle_task(p) || exiting_task(p) || !samples)
			goto done;

	/* Push new 'runtime' value onto stack */
	widx = walt_ravg_hist_size - 1;
	ridx = widx - samples;
	for (; ridx >= 0; --widx, --ridx) {
		hist[widx] = hist[ridx];
		sum += hist[widx];
		if (hist[widx] > max)
			max = hist[widx];
	}

	for (widx = 0; widx < samples && widx < walt_ravg_hist_size; widx++) {
		hist[widx] = runtime;
		sum += hist[widx];
		if (hist[widx] > max)
			max = hist[widx];
	}

	p->ravg.sum = 0;

	if (walt_window_stats_policy == WINDOW_STATS_RECENT) {
		demand = runtime;
	} else if (walt_window_stats_policy == WINDOW_STATS_MAX) {
		demand = max;
	} else {
		avg = div64_u64(sum, walt_ravg_hist_size);
		if (walt_window_stats_policy == WINDOW_STATS_AVG)
			demand = avg;
		else
			demand = max(avg, runtime);
	}

	/*
	 * A throttled deadline sched class task gets dequeued without
	 * changing p->on_rq. Since the dequeue decrements hmp stats
	 * avoid decrementing it here again.
	 */
	if (task_on_rq_queued(p) && (!task_has_dl_policy(p) ||
						!p->dl.dl_throttled))
		fixup_cumulative_runnable_avg(rq, p, demand);

	p->ravg.demand = demand;

done:
	trace_walt_update_history(rq, p, runtime, samples, event);
	return;
}

static void add_to_task_demand(struct rq *rq, struct task_struct *p,
				u64 delta)
{
	delta = scale_exec_time(delta, rq);
	p->ravg.sum += delta;
	if (unlikely(p->ravg.sum > walt_ravg_window))
		p->ravg.sum = walt_ravg_window;
}

/*
 * Account cpu demand of task and/or update task's cpu demand history
 *
 * ms = p->ravg.mark_start;
 * wc = wallclock
 * ws = rq->window_start
 *
 * Three possibilities:
 *
 *	a) Task event is contained within one window.
 *		window_start < mark_start < wallclock
 *
 *		ws   ms  wc
 *		|    |   |
 *		V    V   V
 *		|---------------|
 *
 *	In this case, p->ravg.sum is updated *iff* event is appropriate
 *	(ex: event == PUT_PREV_TASK)
 *
 *	b) Task event spans two windows.
 *		mark_start < window_start < wallclock
 *
 *		ms   ws   wc
 *		|    |    |
 *		V    V    V
 *		-----|-------------------
 *
 *	In this case, p->ravg.sum is updated with (ws - ms) *iff* event
 *	is appropriate, then a new window sample is recorded followed
 *	by p->ravg.sum being set to (wc - ws) *iff* event is appropriate.
 *
 *	c) Task event spans more than two windows.
 *
 *		ms ws_tmp			   ws  wc
 *		|  |				   |   |
 *		V  V				   V   V
 *		---|-------|-------|-------|-------|------
 *		   |				   |
 *		   |<------ nr_full_windows ------>|
 *
 *	In this case, p->ravg.sum is updated with (ws_tmp - ms) first *iff*
 *	event is appropriate, window sample of p->ravg.sum is recorded,
 *	'nr_full_window' samples of window_size is also recorded *iff*
 *	event is appropriate and finally p->ravg.sum is set to (wc - ws)
 *	*iff* event is appropriate.
 *
 * IMPORTANT : Leave p->ravg.mark_start unchanged, as update_cpu_busy_time()
 * depends on it!
 */
static void update_task_demand(struct task_struct *p, struct rq *rq,
	     int event, u64 wallclock)
{
	u64 mark_start = p->ravg.mark_start;
	u64 delta, window_start = rq->window_start;
	int new_window, nr_full_windows;
	u32 window_size = walt_ravg_window;

	new_window = mark_start < window_start;
	if (!account_busy_for_task_demand(p, event)) {
		if (new_window)
			/* If the time accounted isn't being accounted as
			 * busy time, and a new window started, only the
			 * previous window need be closed out with the
			 * pre-existing demand. Multiple windows may have
			 * elapsed, but since empty windows are dropped,
			 * it is not necessary to account those. */
			update_history(rq, p, p->ravg.sum, 1, event);
		return;
	}

	if (!new_window) {
		/* The simple case - busy time contained within the existing
		 * window. */
		add_to_task_demand(rq, p, wallclock - mark_start);
		return;
	}

	/* Busy time spans at least two windows. Temporarily rewind
	 * window_start to first window boundary after mark_start. */
	delta = window_start - mark_start;
	nr_full_windows = div64_u64(delta, window_size);
	window_start -= (u64)nr_full_windows * (u64)window_size;

	/* Process (window_start - mark_start) first */
	add_to_task_demand(rq, p, window_start - mark_start);

	/* Push new sample(s) into task's demand history */
	update_history(rq, p, p->ravg.sum, 1, event);
	if (nr_full_windows)
		update_history(rq, p, scale_exec_time(window_size, rq),
			       nr_full_windows, event);

	/* Roll window_start back to current to process any remainder
	 * in current window. */
	window_start += (u64)nr_full_windows * (u64)window_size;

	/* Process (wallclock - window_start) next */
	mark_start = window_start;
	add_to_task_demand(rq, p, wallclock - mark_start);
}

/* Reflect task activity on its demand and cpu's busy time statistics */
void walt_update_task_ravg(struct task_struct *p, struct rq *rq,
	     int event, u64 wallclock, u64 irqtime)
{
	if (walt_disabled || !rq->window_start)
		return;

	lockdep_assert_held(&rq->lock);

	update_window_start(rq, wallclock);

	if (!p->ravg.mark_start)
		goto done;

	update_task_demand(p, rq, event, wallclock);
	update_cpu_busy_time(p, rq, event, wallclock, irqtime);

done:
	trace_walt_update_task_ravg(p, rq, event, wallclock, irqtime);

	p->ravg.mark_start = wallclock;
}

unsigned long __weak arch_get_cpu_efficiency(int cpu)
{
	return SCHED_LOAD_SCALE;
}

void walt_init_cpu_efficiency(void)
{
	int i, efficiency;
	unsigned int max = 0, min = UINT_MAX;

	for_each_possible_cpu(i) {
		efficiency = arch_get_cpu_efficiency(i);
		cpu_rq(i)->efficiency = efficiency;

		if (efficiency > max)
			max = efficiency;
		if (efficiency < min)
			min = efficiency;
	}

	if (max)
		max_possible_efficiency = max;

	if (min)
		min_possible_efficiency = min;
}

static void reset_task_stats(struct task_struct *p)
{
	u32 sum = 0;

	if (exiting_task(p))
		sum = EXITING_TASK_MARKER;

	memset(&p->ravg, 0, sizeof(struct ravg));
	/* Retain EXITING_TASK marker */
	p->ravg.sum_history[0] = sum;
}

void walt_mark_task_starting(struct task_struct *p)
{
	u64 wallclock;
	struct rq *rq = task_rq(p);

	if (!rq->window_start) {
		reset_task_stats(p);
		return;
	}

	wallclock = walt_ktime_clock();
	p->ravg.mark_start = wallclock;
}

void walt_set_window_start(struct rq *rq)
{
	int cpu = cpu_of(rq);
	struct rq *sync_rq = cpu_rq(sync_cpu);

	if (rq->window_start)
		return;

	if (cpu == sync_cpu) {
		rq->window_start = walt_ktime_clock();
	} else {
		raw_spin_unlock(&rq->lock);
		double_rq_lock(rq, sync_rq);
		rq->window_start = cpu_rq(sync_cpu)->window_start;
		rq->curr_runnable_sum = rq->prev_runnable_sum = 0;
		raw_spin_unlock(&sync_rq->lock);
	}

	rq->curr->ravg.mark_start = rq->window_start;
}

void walt_migrate_sync_cpu(int cpu)
{
	if (cpu == sync_cpu)
		sync_cpu = smp_processor_id();
}

void walt_fixup_busy_time(struct task_struct *p, int new_cpu)
{
	struct rq *src_rq = task_rq(p);
	struct rq *dest_rq = cpu_rq(new_cpu);
	u64 wallclock;

	if (!p->on_rq && p->state != TASK_WAKING)
		return;

	if (exiting_task(p)) {
		return;
	}

	if (p->state == TASK_WAKING)
		double_rq_lock(src_rq, dest_rq);

	wallclock = walt_ktime_clock();

	walt_update_task_ravg(task_rq(p)->curr, task_rq(p),
			TASK_UPDATE, wallclock, 0);
	walt_update_task_ravg(dest_rq->curr, dest_rq,
			TASK_UPDATE, wallclock, 0);

	walt_update_task_ravg(p, task_rq(p), TASK_MIGRATE, wallclock, 0);

	if (p->ravg.curr_window) {
		src_rq->curr_runnable_sum -= p->ravg.curr_window;
		dest_rq->curr_runnable_sum += p->ravg.curr_window;
	}

	if (p->ravg.prev_window) {
		src_rq->prev_runnable_sum -= p->ravg.prev_window;
		dest_rq->prev_runnable_sum += p->ravg.prev_window;
	}

	if ((s64)src_rq->prev_runnable_sum < 0) {
		src_rq->prev_runnable_sum = 0;
		WARN_ON(1);
	}
	if ((s64)src_rq->curr_runnable_sum < 0) {
		src_rq->curr_runnable_sum = 0;
		WARN_ON(1);
	}

	trace_walt_migration_update_sum(src_rq, p);
	trace_walt_migration_update_sum(dest_rq, p);

	if (p->state == TASK_WAKING)
		double_rq_unlock(src_rq, dest_rq);
}

/*
 * Return 'capacity' of a cpu in reference to "least" efficient cpu, such that
 * least efficient cpu gets capacity of 1024
 */
static unsigned long capacity_scale_cpu_efficiency(int cpu)
{
	return (1024 * cpu_rq(cpu)->efficiency) / min_possible_efficiency;
}

/*
 * Return 'capacity' of a cpu in reference to cpu with lowest max_freq
 * (min_max_freq), such that one with lowest max_freq gets capacity of 1024.
 */
static unsigned long capacity_scale_cpu_freq(int cpu)
{
	return (1024 * cpu_rq(cpu)->max_freq) / min_max_freq;
}

/*
 * Return load_scale_factor of a cpu in reference to "most" efficient cpu, so
 * that "most" efficient cpu gets a load_scale_factor of 1
 */
static unsigned long load_scale_cpu_efficiency(int cpu)
{
	return DIV_ROUND_UP(1024 * max_possible_efficiency,
			    cpu_rq(cpu)->efficiency);
}

/*
 * Return load_scale_factor of a cpu in reference to cpu with best max_freq
 * (max_possible_freq), so that one with best max_freq gets a load_scale_factor
 * of 1.
 */
static unsigned long load_scale_cpu_freq(int cpu)
{
	return DIV_ROUND_UP(1024 * max_possible_freq, cpu_rq(cpu)->max_freq);
}

static int compute_capacity(int cpu)
{
	int capacity = 1024;

	capacity *= capacity_scale_cpu_efficiency(cpu);
	capacity >>= 10;

	capacity *= capacity_scale_cpu_freq(cpu);
	capacity >>= 10;

	return capacity;
}

static int compute_load_scale_factor(int cpu)
{
	int load_scale = 1024;

	/*
	 * load_scale_factor accounts for the fact that task load
	 * is in reference to "best" performing cpu. Task's load will need to be
	 * scaled (up) by a factor to determine suitability to be placed on a
	 * (little) cpu.
	 */
	load_scale *= load_scale_cpu_efficiency(cpu);
	load_scale >>= 10;

	load_scale *= load_scale_cpu_freq(cpu);
	load_scale >>= 10;

	return load_scale;
}

static int cpufreq_notifier_policy(struct notifier_block *nb,
		unsigned long val, void *data)
{
	struct cpufreq_policy *policy = (struct cpufreq_policy *)data;
	int i, update_max = 0;
	u64 highest_mpc = 0, highest_mplsf = 0;
	const struct cpumask *cpus = policy->related_cpus;
	unsigned int orig_min_max_freq = min_max_freq;
	unsigned int orig_max_possible_freq = max_possible_freq;
	/* Initialized to policy->max in case policy->related_cpus is empty! */
	unsigned int orig_max_freq = policy->max;

	if (val != CPUFREQ_NOTIFY)
		return 0;

	for_each_cpu(i, policy->related_cpus) {
		cpumask_copy(&cpu_rq(i)->freq_domain_cpumask,
			     policy->related_cpus);
		orig_max_freq = cpu_rq(i)->max_freq;
		cpu_rq(i)->min_freq = policy->min;
		cpu_rq(i)->max_freq = policy->max;
		cpu_rq(i)->cur_freq = policy->cur;
		cpu_rq(i)->max_possible_freq = policy->cpuinfo.max_freq;
	}

	max_possible_freq = max(max_possible_freq, policy->cpuinfo.max_freq);
	if (min_max_freq == 1)
		min_max_freq = UINT_MAX;
	min_max_freq = min(min_max_freq, policy->cpuinfo.max_freq);
	BUG_ON(!min_max_freq);
	BUG_ON(!policy->max);

	/* Changes to policy other than max_freq don't require any updates */
	if (orig_max_freq == policy->max)
		return 0;

	/*
	 * A changed min_max_freq or max_possible_freq (possible during bootup)
	 * needs to trigger re-computation of load_scale_factor and capacity for
	 * all possible cpus (even those offline). It also needs to trigger
	 * re-computation of nr_big_task count on all online cpus.
	 *
	 * A changed rq->max_freq otoh needs to trigger re-computation of
	 * load_scale_factor and capacity for just the cluster of cpus involved.
	 * Since small task definition depends on max_load_scale_factor, a
	 * changed load_scale_factor of one cluster could influence
	 * classification of tasks in another cluster. Hence a changed
	 * rq->max_freq will need to trigger re-computation of nr_big_task
	 * count on all online cpus.
	 *
	 * While it should be sufficient for nr_big_tasks to be
	 * re-computed for only online cpus, we have inadequate context
	 * information here (in policy notifier) with regard to hotplug-safety
	 * context in which notification is issued. As a result, we can't use
	 * get_online_cpus() here, as it can lead to deadlock. Until cpufreq is
	 * fixed up to issue notification always in hotplug-safe context,
	 * re-compute nr_big_task for all possible cpus.
	 */

	if (orig_min_max_freq != min_max_freq ||
		orig_max_possible_freq != max_possible_freq) {
			cpus = cpu_possible_mask;
			update_max = 1;
	}

	/*
	 * Changed load_scale_factor can trigger reclassification of tasks as
	 * big or small. Make this change "atomic" so that tasks are accounted
	 * properly due to changed load_scale_factor
	 */
	for_each_cpu(i, cpus) {
		struct rq *rq = cpu_rq(i);

		rq->capacity = compute_capacity(i);
		rq->load_scale_factor = compute_load_scale_factor(i);

		if (update_max) {
			u64 mpc, mplsf;

			mpc = div_u64(((u64) rq->capacity) *
				rq->max_possible_freq, rq->max_freq);
			rq->max_possible_capacity = (int) mpc;

			mplsf = div_u64(((u64) rq->load_scale_factor) *
				rq->max_possible_freq, rq->max_freq);

			if (mpc > highest_mpc) {
				highest_mpc = mpc;
				cpumask_clear(&mpc_mask);
				cpumask_set_cpu(i, &mpc_mask);
			} else if (mpc == highest_mpc) {
				cpumask_set_cpu(i, &mpc_mask);
			}

			if (mplsf > highest_mplsf)
				highest_mplsf = mplsf;
		}
	}

	if (update_max) {
		max_possible_capacity = highest_mpc;
		max_load_scale_factor = highest_mplsf;
	}

	return 0;
}

static int cpufreq_notifier_trans(struct notifier_block *nb,
		unsigned long val, void *data)
{
	struct cpufreq_freqs *freq = (struct cpufreq_freqs *)data;
	unsigned int cpu = freq->cpu, new_freq = freq->new;
	unsigned long flags;
	int i;

	if (val != CPUFREQ_POSTCHANGE)
		return 0;

	BUG_ON(!new_freq);

	if (cpu_rq(cpu)->cur_freq == new_freq)
		return 0;

	for_each_cpu(i, &cpu_rq(cpu)->freq_domain_cpumask) {
		struct rq *rq = cpu_rq(i);

		raw_spin_lock_irqsave(&rq->lock, flags);
		walt_update_task_ravg(rq->curr, rq, TASK_UPDATE,
				      walt_ktime_clock(), 0);
		rq->cur_freq = new_freq;
		raw_spin_unlock_irqrestore(&rq->lock, flags);
	}

	return 0;
}

static struct notifier_block notifier_policy_block = {
	.notifier_call = cpufreq_notifier_policy
};

static struct notifier_block notifier_trans_block = {
	.notifier_call = cpufreq_notifier_trans
};

static int register_sched_callback(void)
{
	int ret;

	ret = cpufreq_register_notifier(&notifier_policy_block,
						CPUFREQ_POLICY_NOTIFIER);

	if (!ret)
		ret = cpufreq_register_notifier(&notifier_trans_block,
						CPUFREQ_TRANSITION_NOTIFIER);

	return 0;
}

/*
 * cpufreq callbacks can be registered at core_initcall or later time.
 * Any registration done prior to that is "forgotten" by cpufreq. See
 * initialization of variable init_cpufreq_transition_notifier_list_called
 * for further information.
 */
core_initcall(register_sched_callback);

void walt_init_new_task_load(struct task_struct *p)
{
	int i;
	u32 init_load_windows =
			div64_u64((u64)sysctl_sched_walt_init_task_load_pct *
                          (u64)walt_ravg_window, 100);
	u32 init_load_pct = current->init_load_pct;

	p->init_load_pct = 0;
	memset(&p->ravg, 0, sizeof(struct ravg));

	if (init_load_pct) {
		init_load_windows = div64_u64((u64)init_load_pct *
			  (u64)walt_ravg_window, 100);
	}

	p->ravg.demand = init_load_windows;
	for (i = 0; i < RAVG_HIST_SIZE_MAX; ++i)
		p->ravg.sum_history[i] = init_load_windows;
}
