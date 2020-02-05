/*
 * Pressure stall information for CPU, memory and IO
 *
 * Copyright (c) 2018 Facebook, Inc.
 * Author: Johannes Weiner <hannes@cmpxchg.org>
 *
 * Polling support by Suren Baghdasaryan <surenb@google.com>
 * Copyright (c) 2018 Google, Inc.
 *
 * When CPU, memory and IO are contended, tasks experience delays that
 * reduce throughput and introduce latencies into the workload. Memory
 * and IO contention, in addition, can cause a full loss of forward
 * progress in which the CPU goes idle.
 *
 * This code aggregates individual task delays into resource pressure
 * metrics that indicate problems with both workload health and
 * resource utilization.
 *
 *			Model
 *
 * The time in which a task can execute on a CPU is our baseline for
 * productivity. Pressure expresses the amount of time in which this
 * potential cannot be realized due to resource contention.
 *
 * This concept of productivity has two components: the workload and
 * the CPU. To measure the impact of pressure on both, we define two
 * contention states for a resource: SOME and FULL.
 *
 * In the SOME state of a given resource, one or more tasks are
 * delayed on that resource. This affects the workload's ability to
 * perform work, but the CPU may still be executing other tasks.
 *
 * In the FULL state of a given resource, all non-idle tasks are
 * delayed on that resource such that nobody is advancing and the CPU
 * goes idle. This leaves both workload and CPU unproductive.
 *
 * (Naturally, the FULL state doesn't exist for the CPU resource.)
 *
 *	SOME = nr_delayed_tasks != 0
 *	FULL = nr_delayed_tasks != 0 && nr_running_tasks == 0
 *
 * The percentage of wallclock time spent in those compound stall
 * states gives pressure numbers between 0 and 100 for each resource,
 * where the SOME percentage indicates workload slowdowns and the FULL
 * percentage indicates reduced CPU utilization:
 *
 *	%SOME = time(SOME) / period
 *	%FULL = time(FULL) / period
 *
 *			Multiple CPUs
 *
 * The more tasks and available CPUs there are, the more work can be
 * performed concurrently. This means that the potential that can go
 * unrealized due to resource contention *also* scales with non-idle
 * tasks and CPUs.
 *
 * Consider a scenario where 257 number crunching tasks are trying to
 * run concurrently on 256 CPUs. If we simply aggregated the task
 * states, we would have to conclude a CPU SOME pressure number of
 * 100%, since *somebody* is waiting on a runqueue at all
 * times. However, that is clearly not the amount of contention the
 * workload is experiencing: only one out of 256 possible exceution
 * threads will be contended at any given time, or about 0.4%.
 *
 * Conversely, consider a scenario of 4 tasks and 4 CPUs where at any
 * given time *one* of the tasks is delayed due to a lack of memory.
 * Again, looking purely at the task state would yield a memory FULL
 * pressure number of 0%, since *somebody* is always making forward
 * progress. But again this wouldn't capture the amount of execution
 * potential lost, which is 1 out of 4 CPUs, or 25%.
 *
 * To calculate wasted potential (pressure) with multiple processors,
 * we have to base our calculation on the number of non-idle tasks in
 * conjunction with the number of available CPUs, which is the number
 * of potential execution threads. SOME becomes then the proportion of
 * delayed tasks to possibe threads, and FULL is the share of possible
 * threads that are unproductive due to delays:
 *
 *	threads = min(nr_nonidle_tasks, nr_cpus)
 *	   SOME = min(nr_delayed_tasks / threads, 1)
 *	   FULL = (threads - min(nr_running_tasks, threads)) / threads
 *
 * For the 257 number crunchers on 256 CPUs, this yields:
 *
 *	threads = min(257, 256)
 *	   SOME = min(1 / 256, 1)             = 0.4%
 *	   FULL = (256 - min(257, 256)) / 256 = 0%
 *
 * For the 1 out of 4 memory-delayed tasks, this yields:
 *
 *	threads = min(4, 4)
 *	   SOME = min(1 / 4, 1)               = 25%
 *	   FULL = (4 - min(3, 4)) / 4         = 25%
 *
 * [ Substitute nr_cpus with 1, and you can see that it's a natural
 *   extension of the single-CPU model. ]
 *
 *			Implementation
 *
 * To assess the precise time spent in each such state, we would have
 * to freeze the system on task changes and start/stop the state
 * clocks accordingly. Obviously that doesn't scale in practice.
 *
 * Because the scheduler aims to distribute the compute load evenly
 * among the available CPUs, we can track task state locally to each
 * CPU and, at much lower frequency, extrapolate the global state for
 * the cumulative stall times and the running averages.
 *
 * For each runqueue, we track:
 *
 *	   tSOME[cpu] = time(nr_delayed_tasks[cpu] != 0)
 *	   tFULL[cpu] = time(nr_delayed_tasks[cpu] && !nr_running_tasks[cpu])
 *	tNONIDLE[cpu] = time(nr_nonidle_tasks[cpu] != 0)
 *
 * and then periodically aggregate:
 *
 *	tNONIDLE = sum(tNONIDLE[i])
 *
 *	   tSOME = sum(tSOME[i] * tNONIDLE[i]) / tNONIDLE
 *	   tFULL = sum(tFULL[i] * tNONIDLE[i]) / tNONIDLE
 *
 *	   %SOME = tSOME / period
 *	   %FULL = tFULL / period
 *
 * This gives us an approximation of pressure that is practical
 * cost-wise, yet way more sensitive and accurate than periodic
 * sampling of the aggregate task states would be.
 */

#include "../workqueue_internal.h"
#include <linux/sched/loadavg.h>
#include <linux/seq_file.h>
#include <linux/proc_fs.h>
#include <linux/seqlock.h>
#include <linux/uaccess.h>
#include <linux/cgroup.h>
#include <linux/module.h>
#include <linux/sched.h>
#include <linux/ctype.h>
#include <linux/file.h>
#include <linux/poll.h>
#include <linux/psi.h>
#include "sched.h"

static int psi_bug __read_mostly;

DEFINE_STATIC_KEY_FALSE(psi_disabled);

#ifdef CONFIG_PSI_DEFAULT_DISABLED
static bool psi_enable;
#else
static bool psi_enable = true;
#endif
static int __init setup_psi(char *str)
{
	return kstrtobool(str, &psi_enable) == 0;
}
__setup("psi=", setup_psi);

/* Running averages - we need to be higher-res than loadavg */
#define PSI_FREQ	(2*HZ+1)	/* 2 sec intervals */
#define EXP_10s		1677		/* 1/exp(2s/10s) as fixed-point */
#define EXP_60s		1981		/* 1/exp(2s/60s) */
#define EXP_300s	2034		/* 1/exp(2s/300s) */

/* PSI trigger definitions */
#define WINDOW_MIN_US 500000	/* Min window size is 500ms */
#define WINDOW_MAX_US 10000000	/* Max window size is 10s */
#define UPDATES_PER_WINDOW 10	/* 10 updates per window */

/* Sampling frequency in nanoseconds */
static u64 psi_period __read_mostly;

/* System-level pressure and stall tracking */
static DEFINE_PER_CPU(struct psi_group_cpu, system_group_pcpu);
struct psi_group psi_system = {
	.pcpu = &system_group_pcpu,
};

static void psi_avgs_work(struct work_struct *work);

static void group_init(struct psi_group *group)
{
	int cpu;

	for_each_possible_cpu(cpu)
		seqcount_init(&per_cpu_ptr(group->pcpu, cpu)->seq);
	group->avg_last_update = sched_clock();
	group->avg_next_update = group->avg_last_update + psi_period;
	INIT_DELAYED_WORK(&group->avgs_work, psi_avgs_work);
	mutex_init(&group->avgs_lock);
	/* Init trigger-related members */
	atomic_set(&group->poll_scheduled, 0);
	mutex_init(&group->trigger_lock);
	INIT_LIST_HEAD(&group->triggers);
	memset(group->nr_triggers, 0, sizeof(group->nr_triggers));
	group->poll_states = 0;
	group->poll_min_period = U32_MAX;
	memset(group->polling_total, 0, sizeof(group->polling_total));
	group->polling_next_update = ULLONG_MAX;
	group->polling_until = 0;
	rcu_assign_pointer(group->poll_kworker, NULL);
}

void __init psi_init(void)
{
	if (!psi_enable) {
		static_branch_enable(&psi_disabled);
		return;
	}

	psi_period = jiffies_to_nsecs(PSI_FREQ);
	group_init(&psi_system);
}

static bool test_state(unsigned int *tasks, enum psi_states state)
{
	switch (state) {
	case PSI_IO_SOME:
		return tasks[NR_IOWAIT];
	case PSI_IO_FULL:
		return tasks[NR_IOWAIT] && !tasks[NR_RUNNING];
	case PSI_MEM_SOME:
		return tasks[NR_MEMSTALL];
	case PSI_MEM_FULL:
		return tasks[NR_MEMSTALL] && !tasks[NR_RUNNING];
	case PSI_CPU_SOME:
		return tasks[NR_RUNNING] > 1;
	case PSI_NONIDLE:
		return tasks[NR_IOWAIT] || tasks[NR_MEMSTALL] ||
			tasks[NR_RUNNING];
	default:
		return false;
	}
}

static void get_recent_times(struct psi_group *group, int cpu,
			     enum psi_aggregators aggregator, u32 *times,
			     u32 *pchanged_states)
{
	struct psi_group_cpu *groupc = per_cpu_ptr(group->pcpu, cpu);
	u64 now, state_start;
	enum psi_states s;
	unsigned int seq;
	u32 state_mask;

	*pchanged_states = 0;

	/* Snapshot a coherent view of the CPU state */
	do {
		seq = read_seqcount_begin(&groupc->seq);
		now = cpu_clock(cpu);
		memcpy(times, groupc->times, sizeof(groupc->times));
		state_mask = groupc->state_mask;
		state_start = groupc->state_start;
	} while (read_seqcount_retry(&groupc->seq, seq));

	/* Calculate state time deltas against the previous snapshot */
	for (s = 0; s < NR_PSI_STATES; s++) {
		u32 delta;
		/*
		 * In addition to already concluded states, we also
		 * incorporate currently active states on the CPU,
		 * since states may last for many sampling periods.
		 *
		 * This way we keep our delta sampling buckets small
		 * (u32) and our reported pressure close to what's
		 * actually happening.
		 */
		if (state_mask & (1 << s))
			times[s] += now - state_start;

		delta = times[s] - groupc->times_prev[aggregator][s];
		groupc->times_prev[aggregator][s] = times[s];

		times[s] = delta;
		if (delta)
			*pchanged_states |= (1 << s);
	}
}

static void calc_avgs(unsigned long avg[3], int missed_periods,
		      u64 time, u64 period)
{
	unsigned long pct;

	/* Fill in zeroes for periods of no activity */
	if (missed_periods) {
		avg[0] = calc_load_n(avg[0], EXP_10s, 0, missed_periods);
		avg[1] = calc_load_n(avg[1], EXP_60s, 0, missed_periods);
		avg[2] = calc_load_n(avg[2], EXP_300s, 0, missed_periods);
	}

	/* Sample the most recent active period */
	pct = div_u64(time * 100, period);
	pct *= FIXED_1;
	avg[0] = calc_load(avg[0], EXP_10s, pct);
	avg[1] = calc_load(avg[1], EXP_60s, pct);
	avg[2] = calc_load(avg[2], EXP_300s, pct);
}

static void collect_percpu_times(struct psi_group *group,
				 enum psi_aggregators aggregator,
				 u32 *pchanged_states)
{
	u64 deltas[NR_PSI_STATES - 1] = { 0, };
	unsigned long nonidle_total = 0;
	u32 changed_states = 0;
	int cpu;
	int s;

	/*
	 * Collect the per-cpu time buckets and average them into a
	 * single time sample that is normalized to wallclock time.
	 *
	 * For averaging, each CPU is weighted by its non-idle time in
	 * the sampling period. This eliminates artifacts from uneven
	 * loading, or even entirely idle CPUs.
	 */
	for_each_possible_cpu(cpu) {
		u32 times[NR_PSI_STATES];
		u32 nonidle;
		u32 cpu_changed_states;

		get_recent_times(group, cpu, aggregator, times,
				&cpu_changed_states);
		changed_states |= cpu_changed_states;

		nonidle = nsecs_to_jiffies(times[PSI_NONIDLE]);
		nonidle_total += nonidle;

		for (s = 0; s < PSI_NONIDLE; s++)
			deltas[s] += (u64)times[s] * nonidle;
	}

	/*
	 * Integrate the sample into the running statistics that are
	 * reported to userspace: the cumulative stall times and the
	 * decaying averages.
	 *
	 * Pressure percentages are sampled at PSI_FREQ. We might be
	 * called more often when the user polls more frequently than
	 * that; we might be called less often when there is no task
	 * activity, thus no data, and clock ticks are sporadic. The
	 * below handles both.
	 */

	/* total= */
	for (s = 0; s < NR_PSI_STATES - 1; s++)
		group->total[aggregator][s] +=
				div_u64(deltas[s], max(nonidle_total, 1UL));

	if (pchanged_states)
		*pchanged_states = changed_states;
}

static u64 update_averages(struct psi_group *group, u64 now)
{
	unsigned long missed_periods = 0;
	u64 expires, period;
	u64 avg_next_update;
	int s;

	/* avgX= */
	expires = group->avg_next_update;
	if (now - expires >= psi_period)
		missed_periods = div_u64(now - expires, psi_period);

	/*
	 * The periodic clock tick can get delayed for various
	 * reasons, especially on loaded systems. To avoid clock
	 * drift, we schedule the clock in fixed psi_period intervals.
	 * But the deltas we sample out of the per-cpu buckets above
	 * are based on the actual time elapsing between clock ticks.
	 */
	avg_next_update = expires + ((1 + missed_periods) * psi_period);
	period = now - (group->avg_last_update + (missed_periods * psi_period));
	group->avg_last_update = now;

	for (s = 0; s < NR_PSI_STATES - 1; s++) {
		u32 sample;

		sample = group->total[PSI_AVGS][s] - group->avg_total[s];
		/*
		 * Due to the lockless sampling of the time buckets,
		 * recorded time deltas can slip into the next period,
		 * which under full pressure can result in samples in
		 * excess of the period length.
		 *
		 * We don't want to report non-sensical pressures in
		 * excess of 100%, nor do we want to drop such events
		 * on the floor. Instead we punt any overage into the
		 * future until pressure subsides. By doing this we
		 * don't underreport the occurring pressure curve, we
		 * just report it delayed by one period length.
		 *
		 * The error isn't cumulative. As soon as another
		 * delta slips from a period P to P+1, by definition
		 * it frees up its time T in P.
		 */
		if (sample > period)
			sample = period;
		group->avg_total[s] += sample;
		calc_avgs(group->avg[s], missed_periods, sample, period);
	}

	return avg_next_update;
}

static void psi_avgs_work(struct work_struct *work)
{
	struct delayed_work *dwork;
	struct psi_group *group;
	u32 changed_states;
	bool nonidle;
	u64 now;

	dwork = to_delayed_work(work);
	group = container_of(dwork, struct psi_group, avgs_work);

	mutex_lock(&group->avgs_lock);

	now = sched_clock();

	collect_percpu_times(group, PSI_AVGS, &changed_states);
	nonidle = changed_states & (1 << PSI_NONIDLE);
	/*
	 * If there is task activity, periodically fold the per-cpu
	 * times and feed samples into the running averages. If things
	 * are idle and there is no data to process, stop the clock.
	 * Once restarted, we'll catch up the running averages in one
	 * go - see calc_avgs() and missed_periods.
	 */
	if (now >= group->avg_next_update)
		group->avg_next_update = update_averages(group, now);

	if (nonidle) {
		schedule_delayed_work(dwork, nsecs_to_jiffies(
				group->avg_next_update - now) + 1);
	}

	mutex_unlock(&group->avgs_lock);
}

/* Trigger tracking window manupulations */
static void window_reset(struct psi_window *win, u64 now, u64 value,
			 u64 prev_growth)
{
	win->start_time = now;
	win->start_value = value;
	win->prev_growth = prev_growth;
}

/*
 * PSI growth tracking window update and growth calculation routine.
 *
 * This approximates a sliding tracking window by interpolating
 * partially elapsed windows using historical growth data from the
 * previous intervals. This minimizes memory requirements (by not storing
 * all the intermediate values in the previous window) and simplifies
 * the calculations. It works well because PSI signal changes only in
 * positive direction and over relatively small window sizes the growth
 * is close to linear.
 */
static u64 window_update(struct psi_window *win, u64 now, u64 value)
{
	u64 elapsed;
	u64 growth;

	elapsed = now - win->start_time;
	growth = value - win->start_value;
	/*
	 * After each tracking window passes win->start_value and
	 * win->start_time get reset and win->prev_growth stores
	 * the average per-window growth of the previous window.
	 * win->prev_growth is then used to interpolate additional
	 * growth from the previous window assuming it was linear.
	 */
	if (elapsed > win->size)
		window_reset(win, now, value, growth);
	else {
		u32 remaining;

		remaining = win->size - elapsed;
		growth += div64_u64(win->prev_growth * remaining, win->size);
	}

	return growth;
}

static void init_triggers(struct psi_group *group, u64 now)
{
	struct psi_trigger *t;

	list_for_each_entry(t, &group->triggers, node)
		window_reset(&t->win, now,
				group->total[PSI_POLL][t->state], 0);
	memcpy(group->polling_total, group->total[PSI_POLL],
		   sizeof(group->polling_total));
	group->polling_next_update = now + group->poll_min_period;
}

static u64 update_triggers(struct psi_group *group, u64 now)
{
	struct psi_trigger *t;
	bool new_stall = false;
	u64 *total = group->total[PSI_POLL];

	/*
	 * On subsequent updates, calculate growth deltas and let
	 * watchers know when their specified thresholds are exceeded.
	 */
	list_for_each_entry(t, &group->triggers, node) {
		u64 growth;

		/* Check for stall activity */
		if (group->polling_total[t->state] == total[t->state])
			continue;

		/*
		 * Multiple triggers might be looking at the same state,
		 * remember to update group->polling_total[] once we've
		 * been through all of them. Also remember to extend the
		 * polling time if we see new stall activity.
		 */
		new_stall = true;

		/* Calculate growth since last update */
		growth = window_update(&t->win, now, total[t->state]);
		if (growth < t->threshold)
			continue;

		/* Limit event signaling to once per window */
		if (now < t->last_event_time + t->win.size)
			continue;

		/* Generate an event */
		if (cmpxchg(&t->event, 0, 1) == 0)
			wake_up_interruptible(&t->event_wait);
		t->last_event_time = now;
	}

	if (new_stall)
		memcpy(group->polling_total, total,
				sizeof(group->polling_total));

	return now + group->poll_min_period;
}

/*
 * Schedule polling if it's not already scheduled. It's safe to call even from
 * hotpath because even though kthread_queue_delayed_work takes worker->lock
 * spinlock that spinlock is never contended due to poll_scheduled atomic
 * preventing such competition.
 */
static void psi_schedule_poll_work(struct psi_group *group, unsigned long delay)
{
	struct kthread_worker *kworker;

	/* Do not reschedule if already scheduled */
	if (atomic_cmpxchg(&group->poll_scheduled, 0, 1) != 0)
		return;

	rcu_read_lock();

	kworker = rcu_dereference(group->poll_kworker);
	/*
	 * kworker might be NULL in case psi_trigger_destroy races with
	 * psi_task_change (hotpath) which can't use locks
	 */
	if (likely(kworker))
		kthread_queue_delayed_work(kworker, &group->poll_work, delay);
	else
		atomic_set(&group->poll_scheduled, 0);

	rcu_read_unlock();
}

static void psi_poll_work(struct kthread_work *work)
{
	struct kthread_delayed_work *dwork;
	struct psi_group *group;
	u32 changed_states;
	u64 now;

	dwork = container_of(work, struct kthread_delayed_work, work);
	group = container_of(dwork, struct psi_group, poll_work);

	atomic_set(&group->poll_scheduled, 0);

	mutex_lock(&group->trigger_lock);

	now = sched_clock();

	collect_percpu_times(group, PSI_POLL, &changed_states);

	if (changed_states & group->poll_states) {
		/* Initialize trigger windows when entering polling mode */
		if (now > group->polling_until)
			init_triggers(group, now);

		/*
		 * Keep the monitor active for at least the duration of the
		 * minimum tracking window as long as monitor states are
		 * changing.
		 */
		group->polling_until = now +
			group->poll_min_period * UPDATES_PER_WINDOW;
	}

	if (now > group->polling_until) {
		group->polling_next_update = ULLONG_MAX;
		goto out;
	}

	if (now >= group->polling_next_update)
		group->polling_next_update = update_triggers(group, now);

	psi_schedule_poll_work(group,
		nsecs_to_jiffies(group->polling_next_update - now) + 1);

out:
	mutex_unlock(&group->trigger_lock);
}

static void record_times(struct psi_group_cpu *groupc, int cpu,
			 bool memstall_tick)
{
	u32 delta;
	u64 now;

	now = cpu_clock(cpu);
	delta = now - groupc->state_start;
	groupc->state_start = now;

	if (groupc->state_mask & (1 << PSI_IO_SOME)) {
		groupc->times[PSI_IO_SOME] += delta;
		if (groupc->state_mask & (1 << PSI_IO_FULL))
			groupc->times[PSI_IO_FULL] += delta;
	}

	if (groupc->state_mask & (1 << PSI_MEM_SOME)) {
		groupc->times[PSI_MEM_SOME] += delta;
		if (groupc->state_mask & (1 << PSI_MEM_FULL))
			groupc->times[PSI_MEM_FULL] += delta;
		else if (memstall_tick) {
			u32 sample;
			/*
			 * Since we care about lost potential, a
			 * memstall is FULL when there are no other
			 * working tasks, but also when the CPU is
			 * actively reclaiming and nothing productive
			 * could run even if it were runnable.
			 *
			 * When the timer tick sees a reclaiming CPU,
			 * regardless of runnable tasks, sample a FULL
			 * tick (or less if it hasn't been a full tick
			 * since the last state change).
			 */
			sample = min(delta, (u32)jiffies_to_nsecs(1));
			groupc->times[PSI_MEM_FULL] += sample;
		}
	}

	if (groupc->state_mask & (1 << PSI_CPU_SOME))
		groupc->times[PSI_CPU_SOME] += delta;

	if (groupc->state_mask & (1 << PSI_NONIDLE))
		groupc->times[PSI_NONIDLE] += delta;
}

static u32 psi_group_change(struct psi_group *group, int cpu,
			    unsigned int clear, unsigned int set)
{
	struct psi_group_cpu *groupc;
	unsigned int t, m;
	enum psi_states s;
	u32 state_mask = 0;

	groupc = per_cpu_ptr(group->pcpu, cpu);

	/*
	 * First we assess the aggregate resource states this CPU's
	 * tasks have been in since the last change, and account any
	 * SOME and FULL time these may have resulted in.
	 *
	 * Then we update the task counts according to the state
	 * change requested through the @clear and @set bits.
	 */
	write_seqcount_begin(&groupc->seq);

	record_times(groupc, cpu, false);

	for (t = 0, m = clear; m; m &= ~(1 << t), t++) {
		if (!(m & (1 << t)))
			continue;
		if (groupc->tasks[t] == 0 && !psi_bug) {
			printk_deferred(KERN_ERR "psi: task underflow! cpu=%d t=%d tasks=[%u %u %u] clear=%x set=%x\n",
					cpu, t, groupc->tasks[0],
					groupc->tasks[1], groupc->tasks[2],
					clear, set);
			psi_bug = 1;
		}
		groupc->tasks[t]--;
	}

	for (t = 0; set; set &= ~(1 << t), t++)
		if (set & (1 << t))
			groupc->tasks[t]++;

	/* Calculate state mask representing active states */
	for (s = 0; s < NR_PSI_STATES; s++) {
		if (test_state(groupc->tasks, s))
			state_mask |= (1 << s);
	}
	groupc->state_mask = state_mask;

	write_seqcount_end(&groupc->seq);

	return state_mask;
}

static struct psi_group *iterate_groups(struct task_struct *task, void **iter)
{
#ifdef CONFIG_CGROUPS
	struct cgroup *cgroup = NULL;

	if (!*iter)
		cgroup = task->cgroups->dfl_cgrp;
	else if (*iter == &psi_system)
		return NULL;
	else
		cgroup = cgroup_parent(*iter);

	if (cgroup && cgroup_parent(cgroup)) {
		*iter = cgroup;
		return cgroup_psi(cgroup);
	}
#else
	if (*iter)
		return NULL;
#endif
	*iter = &psi_system;
	return &psi_system;
}

void psi_task_change(struct task_struct *task, int clear, int set)
{
	int cpu = task_cpu(task);
	struct psi_group *group;
	bool wake_clock = true;
	void *iter = NULL;

	if (!task->pid)
		return;

	if (((task->psi_flags & set) ||
	     (task->psi_flags & clear) != clear) &&
	    !psi_bug) {
		printk_deferred(KERN_ERR "psi: inconsistent task state! task=%d:%s cpu=%d psi_flags=%x clear=%x set=%x\n",
				task->pid, task->comm, cpu,
				task->psi_flags, clear, set);
		psi_bug = 1;
	}

	task->psi_flags &= ~clear;
	task->psi_flags |= set;

	/*
	 * Periodic aggregation shuts off if there is a period of no
	 * task changes, so we wake it back up if necessary. However,
	 * don't do this if the task change is the aggregation worker
	 * itself going to sleep, or we'll ping-pong forever.
	 */
	if (unlikely((clear & TSK_RUNNING) &&
		     (task->flags & PF_WQ_WORKER) &&
		     wq_worker_last_func(task) == psi_avgs_work))
		wake_clock = false;

	while ((group = iterate_groups(task, &iter))) {
		u32 state_mask = psi_group_change(group, cpu, clear, set);

		if (state_mask & group->poll_states)
			psi_schedule_poll_work(group, 1);

		if (wake_clock && !delayed_work_pending(&group->avgs_work))
			schedule_delayed_work(&group->avgs_work, PSI_FREQ);
	}
}

void psi_memstall_tick(struct task_struct *task, int cpu)
{
	struct psi_group *group;
	void *iter = NULL;

	while ((group = iterate_groups(task, &iter))) {
		struct psi_group_cpu *groupc;

		groupc = per_cpu_ptr(group->pcpu, cpu);
		write_seqcount_begin(&groupc->seq);
		record_times(groupc, cpu, true);
		write_seqcount_end(&groupc->seq);
	}
}

/**
 * psi_memstall_enter - mark the beginning of a memory stall section
 * @flags: flags to handle nested sections
 *
 * Marks the calling task as being stalled due to a lack of memory,
 * such as waiting for a refault or performing reclaim.
 */
void psi_memstall_enter(unsigned long *flags)
{
	struct rq_flags rf;
	struct rq *rq;

	if (static_branch_likely(&psi_disabled))
		return;

	*flags = current->flags & PF_MEMSTALL;
	if (*flags)
		return;
	/*
	 * PF_MEMSTALL setting & accounting needs to be atomic wrt
	 * changes to the task's scheduling state, otherwise we can
	 * race with CPU migration.
	 */
	rq = this_rq_lock_irq(&rf);

	current->flags |= PF_MEMSTALL;
	psi_task_change(current, 0, TSK_MEMSTALL);

	rq_unlock_irq(rq, &rf);
}

/**
 * psi_memstall_leave - mark the end of an memory stall section
 * @flags: flags to handle nested memdelay sections
 *
 * Marks the calling task as no longer stalled due to lack of memory.
 */
void psi_memstall_leave(unsigned long *flags)
{
	struct rq_flags rf;
	struct rq *rq;

	if (static_branch_likely(&psi_disabled))
		return;

	if (*flags)
		return;
	/*
	 * PF_MEMSTALL clearing & accounting needs to be atomic wrt
	 * changes to the task's scheduling state, otherwise we could
	 * race with CPU migration.
	 */
	rq = this_rq_lock_irq(&rf);

	current->flags &= ~PF_MEMSTALL;
	psi_task_change(current, TSK_MEMSTALL, 0);

	rq_unlock_irq(rq, &rf);
}

#ifdef CONFIG_CGROUPS
int psi_cgroup_alloc(struct cgroup *cgroup)
{
	if (static_branch_likely(&psi_disabled))
		return 0;

	cgroup->psi.pcpu = alloc_percpu(struct psi_group_cpu);
	if (!cgroup->psi.pcpu)
		return -ENOMEM;
	group_init(&cgroup->psi);
	return 0;
}

void psi_cgroup_free(struct cgroup *cgroup)
{
	if (static_branch_likely(&psi_disabled))
		return;

	cancel_delayed_work_sync(&cgroup->psi.avgs_work);
	free_percpu(cgroup->psi.pcpu);
	/* All triggers must be removed by now */
	WARN_ONCE(cgroup->psi.poll_states, "psi: trigger leak\n");
}

/**
 * cgroup_move_task - move task to a different cgroup
 * @task: the task
 * @to: the target css_set
 *
 * Move task to a new cgroup and safely migrate its associated stall
 * state between the different groups.
 *
 * This function acquires the task's rq lock to lock out concurrent
 * changes to the task's scheduling state and - in case the task is
 * running - concurrent changes to its stall state.
 */
void cgroup_move_task(struct task_struct *task, struct css_set *to)
{
	unsigned int task_flags = 0;
	struct rq_flags rf;
	struct rq *rq;

	if (static_branch_likely(&psi_disabled)) {
		/*
		 * Lame to do this here, but the scheduler cannot be locked
		 * from the outside, so we move cgroups from inside sched/.
		 */
		rcu_assign_pointer(task->cgroups, to);
		return;
	}

	rq = task_rq_lock(task, &rf);

	if (task_on_rq_queued(task))
		task_flags = TSK_RUNNING;
	else if (task->in_iowait)
		task_flags = TSK_IOWAIT;

	if (task->flags & PF_MEMSTALL)
		task_flags |= TSK_MEMSTALL;

	if (task_flags)
		psi_task_change(task, task_flags, 0);

	/* See comment above */
	rcu_assign_pointer(task->cgroups, to);

	if (task_flags)
		psi_task_change(task, 0, task_flags);

	task_rq_unlock(rq, task, &rf);
}
#endif /* CONFIG_CGROUPS */

int psi_show(struct seq_file *m, struct psi_group *group, enum psi_res res)
{
	int full;
	u64 now;

	if (static_branch_likely(&psi_disabled))
		return -EOPNOTSUPP;

	/* Update averages before reporting them */
	mutex_lock(&group->avgs_lock);
	now = sched_clock();
	collect_percpu_times(group, PSI_AVGS, NULL);
	if (now >= group->avg_next_update)
		group->avg_next_update = update_averages(group, now);
	mutex_unlock(&group->avgs_lock);

	for (full = 0; full < 2 - (res == PSI_CPU); full++) {
		unsigned long avg[3];
		u64 total;
		int w;

		for (w = 0; w < 3; w++)
			avg[w] = group->avg[res * 2 + full][w];
		total = div_u64(group->total[PSI_AVGS][res * 2 + full],
				NSEC_PER_USEC);

		seq_printf(m, "%s avg10=%lu.%02lu avg60=%lu.%02lu avg300=%lu.%02lu total=%llu\n",
			   full ? "full" : "some",
			   LOAD_INT(avg[0]), LOAD_FRAC(avg[0]),
			   LOAD_INT(avg[1]), LOAD_FRAC(avg[1]),
			   LOAD_INT(avg[2]), LOAD_FRAC(avg[2]),
			   total);
	}

	return 0;
}

static int psi_io_show(struct seq_file *m, void *v)
{
	return psi_show(m, &psi_system, PSI_IO);
}

static int psi_memory_show(struct seq_file *m, void *v)
{
	return psi_show(m, &psi_system, PSI_MEM);
}

static int psi_cpu_show(struct seq_file *m, void *v)
{
	return psi_show(m, &psi_system, PSI_CPU);
}

static int psi_io_open(struct inode *inode, struct file *file)
{
	return single_open(file, psi_io_show, NULL);
}

static int psi_memory_open(struct inode *inode, struct file *file)
{
	return single_open(file, psi_memory_show, NULL);
}

static int psi_cpu_open(struct inode *inode, struct file *file)
{
	return single_open(file, psi_cpu_show, NULL);
}

struct psi_trigger *psi_trigger_create(struct psi_group *group,
			char *buf, size_t nbytes, enum psi_res res)
{
	struct psi_trigger *t;
	enum psi_states state;
	u32 threshold_us;
	u32 window_us;

	if (static_branch_likely(&psi_disabled))
		return ERR_PTR(-EOPNOTSUPP);

	if (sscanf(buf, "some %u %u", &threshold_us, &window_us) == 2)
		state = PSI_IO_SOME + res * 2;
	else if (sscanf(buf, "full %u %u", &threshold_us, &window_us) == 2)
		state = PSI_IO_FULL + res * 2;
	else
		return ERR_PTR(-EINVAL);

	if (state >= PSI_NONIDLE)
		return ERR_PTR(-EINVAL);

	if (window_us < WINDOW_MIN_US ||
		window_us > WINDOW_MAX_US)
		return ERR_PTR(-EINVAL);

	/* Check threshold */
	if (threshold_us == 0 || threshold_us > window_us)
		return ERR_PTR(-EINVAL);

	t = kmalloc(sizeof(*t), GFP_KERNEL);
	if (!t)
		return ERR_PTR(-ENOMEM);

	t->group = group;
	t->state = state;
	t->threshold = threshold_us * NSEC_PER_USEC;
	t->win.size = window_us * NSEC_PER_USEC;
	window_reset(&t->win, 0, 0, 0);

	t->event = 0;
	t->last_event_time = 0;
	init_waitqueue_head(&t->event_wait);
	kref_init(&t->refcount);

	mutex_lock(&group->trigger_lock);

	if (!rcu_access_pointer(group->poll_kworker)) {
		struct sched_param param = {
			.sched_priority = 1,
		};
		struct kthread_worker *kworker;

		kworker = kthread_create_worker(0, "psimon");
		if (IS_ERR(kworker)) {
			kfree(t);
			mutex_unlock(&group->trigger_lock);
			return ERR_CAST(kworker);
		}
		sched_setscheduler_nocheck(kworker->task, SCHED_FIFO, &param);
		kthread_init_delayed_work(&group->poll_work,
				psi_poll_work);
		rcu_assign_pointer(group->poll_kworker, kworker);
	}

	list_add(&t->node, &group->triggers);
	group->poll_min_period = min(group->poll_min_period,
		div_u64(t->win.size, UPDATES_PER_WINDOW));
	group->nr_triggers[t->state]++;
	group->poll_states |= (1 << t->state);

	mutex_unlock(&group->trigger_lock);

	return t;
}

static void psi_trigger_destroy(struct kref *ref)
{
	struct psi_trigger *t = container_of(ref, struct psi_trigger, refcount);
	struct psi_group *group = t->group;
	struct kthread_worker *kworker_to_destroy = NULL;

	if (static_branch_likely(&psi_disabled))
		return;

	/*
	 * Wakeup waiters to stop polling. Can happen if cgroup is deleted
	 * from under a polling process.
	 */
	wake_up_interruptible(&t->event_wait);

	mutex_lock(&group->trigger_lock);

	if (!list_empty(&t->node)) {
		struct psi_trigger *tmp;
		u64 period = ULLONG_MAX;

		list_del(&t->node);
		group->nr_triggers[t->state]--;
		if (!group->nr_triggers[t->state])
			group->poll_states &= ~(1 << t->state);
		/* reset min update period for the remaining triggers */
		list_for_each_entry(tmp, &group->triggers, node)
			period = min(period, div_u64(tmp->win.size,
					UPDATES_PER_WINDOW));
		group->poll_min_period = period;
		/* Destroy poll_kworker when the last trigger is destroyed */
		if (group->poll_states == 0) {
			group->polling_until = 0;
			kworker_to_destroy = rcu_dereference_protected(
					group->poll_kworker,
					lockdep_is_held(&group->trigger_lock));
			rcu_assign_pointer(group->poll_kworker, NULL);
		}
	}

	mutex_unlock(&group->trigger_lock);

	/*
	 * Wait for both *trigger_ptr from psi_trigger_replace and
	 * poll_kworker RCUs to complete their read-side critical sections
	 * before destroying the trigger and optionally the poll_kworker
	 */
	synchronize_rcu();
	/*
	 * Destroy the kworker after releasing trigger_lock to prevent a
	 * deadlock while waiting for psi_poll_work to acquire trigger_lock
	 */
	if (kworker_to_destroy) {
		/*
		 * After the RCU grace period has expired, the worker
		 * can no longer be found through group->poll_kworker.
		 * But it might have been already scheduled before
		 * that - deschedule it cleanly before destroying it.
		 */
		kthread_cancel_delayed_work_sync(&group->poll_work);
		atomic_set(&group->poll_scheduled, 0);

		kthread_destroy_worker(kworker_to_destroy);
	}
	kfree(t);
}

void psi_trigger_replace(void **trigger_ptr, struct psi_trigger *new)
{
	struct psi_trigger *old = *trigger_ptr;

	if (static_branch_likely(&psi_disabled))
		return;

	rcu_assign_pointer(*trigger_ptr, new);
	if (old)
		kref_put(&old->refcount, psi_trigger_destroy);
}

__poll_t psi_trigger_poll(void **trigger_ptr,
				struct file *file, poll_table *wait)
{
	__poll_t ret = DEFAULT_POLLMASK;
	struct psi_trigger *t;

	if (static_branch_likely(&psi_disabled))
		return DEFAULT_POLLMASK | EPOLLERR | EPOLLPRI;

	rcu_read_lock();

	t = rcu_dereference(*(void __rcu __force **)trigger_ptr);
	if (!t) {
		rcu_read_unlock();
		return DEFAULT_POLLMASK | EPOLLERR | EPOLLPRI;
	}
	kref_get(&t->refcount);

	rcu_read_unlock();

	poll_wait(file, &t->event_wait, wait);

	if (cmpxchg(&t->event, 1, 0) == 1)
		ret |= EPOLLPRI;

	kref_put(&t->refcount, psi_trigger_destroy);

	return ret;
}

static ssize_t psi_write(struct file *file, const char __user *user_buf,
			 size_t nbytes, enum psi_res res)
{
	char buf[32];
	size_t buf_size;
	struct seq_file *seq;
	struct psi_trigger *new;

	if (static_branch_likely(&psi_disabled))
		return -EOPNOTSUPP;

	buf_size = min(nbytes, sizeof(buf));
	if (copy_from_user(buf, user_buf, buf_size))
		return -EFAULT;

	buf[buf_size - 1] = '\0';

	new = psi_trigger_create(&psi_system, buf, nbytes, res);
	if (IS_ERR(new))
		return PTR_ERR(new);

	seq = file->private_data;
	/* Take seq->lock to protect seq->private from concurrent writes */
	mutex_lock(&seq->lock);
	psi_trigger_replace(&seq->private, new);
	mutex_unlock(&seq->lock);

	return nbytes;
}

static ssize_t psi_io_write(struct file *file, const char __user *user_buf,
			    size_t nbytes, loff_t *ppos)
{
	return psi_write(file, user_buf, nbytes, PSI_IO);
}

static ssize_t psi_memory_write(struct file *file, const char __user *user_buf,
				size_t nbytes, loff_t *ppos)
{
	return psi_write(file, user_buf, nbytes, PSI_MEM);
}

static ssize_t psi_cpu_write(struct file *file, const char __user *user_buf,
			     size_t nbytes, loff_t *ppos)
{
	return psi_write(file, user_buf, nbytes, PSI_CPU);
}

static __poll_t psi_fop_poll(struct file *file, poll_table *wait)
{
	struct seq_file *seq = file->private_data;

	return psi_trigger_poll(&seq->private, file, wait);
}

static int psi_fop_release(struct inode *inode, struct file *file)
{
	struct seq_file *seq = file->private_data;

	psi_trigger_replace(&seq->private, NULL);
	return single_release(inode, file);
}

static const struct file_operations psi_io_fops = {
	.open           = psi_io_open,
	.read           = seq_read,
	.llseek         = seq_lseek,
	.write          = psi_io_write,
	.poll           = psi_fop_poll,
	.release        = psi_fop_release,
};

static const struct file_operations psi_memory_fops = {
	.open           = psi_memory_open,
	.read           = seq_read,
	.llseek         = seq_lseek,
	.write          = psi_memory_write,
	.poll           = psi_fop_poll,
	.release        = psi_fop_release,
};

static const struct file_operations psi_cpu_fops = {
	.open           = psi_cpu_open,
	.read           = seq_read,
	.llseek         = seq_lseek,
	.write          = psi_cpu_write,
	.poll           = psi_fop_poll,
	.release        = psi_fop_release,
};

static int __init psi_proc_init(void)
{
	if (psi_enable) {
		proc_mkdir("pressure", NULL);
		proc_create("pressure/io", 0, NULL, &psi_io_fops);
		proc_create("pressure/memory", 0, NULL, &psi_memory_fops);
		proc_create("pressure/cpu", 0, NULL, &psi_cpu_fops);
	}
	return 0;
}
module_init(psi_proc_init);
