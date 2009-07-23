/*
 * Completely Fair Scheduling (CFS) Class (SCHED_NORMAL/SCHED_BATCH)
 *
 *  Copyright (C) 2007 Red Hat, Inc., Ingo Molnar <mingo@redhat.com>
 *
 *  Interactivity improvements by Mike Galbraith
 *  (C) 2007 Mike Galbraith <efault@gmx.de>
 *
 *  Various enhancements by Dmitry Adamushko.
 *  (C) 2007 Dmitry Adamushko <dmitry.adamushko@gmail.com>
 *
 *  Group scheduling enhancements by Srivatsa Vaddagiri
 *  Copyright IBM Corporation, 2007
 *  Author: Srivatsa Vaddagiri <vatsa@linux.vnet.ibm.com>
 *
 *  Scaled math optimizations by Thomas Gleixner
 *  Copyright (C) 2007, Thomas Gleixner <tglx@linutronix.de>
 *
 *  Adaptive scheduling granularity, math enhancements by Peter Zijlstra
 *  Copyright (C) 2007 Red Hat, Inc., Peter Zijlstra <pzijlstr@redhat.com>
 */

#include <linux/latencytop.h>

/*
 * Targeted preemption latency for CPU-bound tasks:
 * (default: 20ms * (1 + ilog(ncpus)), units: nanoseconds)
 *
 * NOTE: this latency value is not the same as the concept of
 * 'timeslice length' - timeslices in CFS are of variable length
 * and have no persistent notion like in traditional, time-slice
 * based scheduling concepts.
 *
 * (to see the precise effective timeslice length of your workload,
 *  run vmstat and monitor the context-switches (cs) field)
 */
unsigned int sysctl_sched_latency = 20000000ULL;

/*
 * Minimal preemption granularity for CPU-bound tasks:
 * (default: 4 msec * (1 + ilog(ncpus)), units: nanoseconds)
 */
unsigned int sysctl_sched_min_granularity = 4000000ULL;

/*
 * is kept at sysctl_sched_latency / sysctl_sched_min_granularity
 */
static unsigned int sched_nr_latency = 5;

/*
 * After fork, child runs first. (default) If set to 0 then
 * parent will (try to) run first.
 */
const_debug unsigned int sysctl_sched_child_runs_first = 1;

/*
 * sys_sched_yield() compat mode
 *
 * This option switches the agressive yield implementation of the
 * old scheduler back on.
 */
unsigned int __read_mostly sysctl_sched_compat_yield;

/*
 * SCHED_OTHER wake-up granularity.
 * (default: 5 msec * (1 + ilog(ncpus)), units: nanoseconds)
 *
 * This option delays the preemption effects of decoupled workloads
 * and reduces their over-scheduling. Synchronous workloads will still
 * have immediate wakeup/sleep latencies.
 */
unsigned int sysctl_sched_wakeup_granularity = 5000000UL;

const_debug unsigned int sysctl_sched_migration_cost = 500000UL;

static const struct sched_class fair_sched_class;

/**************************************************************
 * CFS operations on generic schedulable entities:
 */

static inline struct task_struct *task_of(struct sched_entity *se)
{
	return container_of(se, struct task_struct, se);
}

#ifdef CONFIG_FAIR_GROUP_SCHED

/* cpu runqueue to which this cfs_rq is attached */
static inline struct rq *rq_of(struct cfs_rq *cfs_rq)
{
	return cfs_rq->rq;
}

/* An entity is a task if it doesn't "own" a runqueue */
#define entity_is_task(se)	(!se->my_q)

/* Walk up scheduling entities hierarchy */
#define for_each_sched_entity(se) \
		for (; se; se = se->parent)

static inline struct cfs_rq *task_cfs_rq(struct task_struct *p)
{
	return p->se.cfs_rq;
}

/* runqueue on which this entity is (to be) queued */
static inline struct cfs_rq *cfs_rq_of(struct sched_entity *se)
{
	return se->cfs_rq;
}

/* runqueue "owned" by this group */
static inline struct cfs_rq *group_cfs_rq(struct sched_entity *grp)
{
	return grp->my_q;
}

/* Given a group's cfs_rq on one cpu, return its corresponding cfs_rq on
 * another cpu ('this_cpu')
 */
static inline struct cfs_rq *cpu_cfs_rq(struct cfs_rq *cfs_rq, int this_cpu)
{
	return cfs_rq->tg->cfs_rq[this_cpu];
}

/* Iterate thr' all leaf cfs_rq's on a runqueue */
#define for_each_leaf_cfs_rq(rq, cfs_rq) \
	list_for_each_entry_rcu(cfs_rq, &rq->leaf_cfs_rq_list, leaf_cfs_rq_list)

/* Do the two (enqueued) entities belong to the same group ? */
static inline int
is_same_group(struct sched_entity *se, struct sched_entity *pse)
{
	if (se->cfs_rq == pse->cfs_rq)
		return 1;

	return 0;
}

static inline struct sched_entity *parent_entity(struct sched_entity *se)
{
	return se->parent;
}

/* return depth at which a sched entity is present in the hierarchy */
static inline int depth_se(struct sched_entity *se)
{
	int depth = 0;

	for_each_sched_entity(se)
		depth++;

	return depth;
}

static void
find_matching_se(struct sched_entity **se, struct sched_entity **pse)
{
	int se_depth, pse_depth;

	/*
	 * preemption test can be made between sibling entities who are in the
	 * same cfs_rq i.e who have a common parent. Walk up the hierarchy of
	 * both tasks until we find their ancestors who are siblings of common
	 * parent.
	 */

	/* First walk up until both entities are at same depth */
	se_depth = depth_se(*se);
	pse_depth = depth_se(*pse);

	while (se_depth > pse_depth) {
		se_depth--;
		*se = parent_entity(*se);
	}

	while (pse_depth > se_depth) {
		pse_depth--;
		*pse = parent_entity(*pse);
	}

	while (!is_same_group(*se, *pse)) {
		*se = parent_entity(*se);
		*pse = parent_entity(*pse);
	}
}

#else	/* CONFIG_FAIR_GROUP_SCHED */

static inline struct rq *rq_of(struct cfs_rq *cfs_rq)
{
	return container_of(cfs_rq, struct rq, cfs);
}

#define entity_is_task(se)	1

#define for_each_sched_entity(se) \
		for (; se; se = NULL)

static inline struct cfs_rq *task_cfs_rq(struct task_struct *p)
{
	return &task_rq(p)->cfs;
}

static inline struct cfs_rq *cfs_rq_of(struct sched_entity *se)
{
	struct task_struct *p = task_of(se);
	struct rq *rq = task_rq(p);

	return &rq->cfs;
}

/* runqueue "owned" by this group */
static inline struct cfs_rq *group_cfs_rq(struct sched_entity *grp)
{
	return NULL;
}

static inline struct cfs_rq *cpu_cfs_rq(struct cfs_rq *cfs_rq, int this_cpu)
{
	return &cpu_rq(this_cpu)->cfs;
}

#define for_each_leaf_cfs_rq(rq, cfs_rq) \
		for (cfs_rq = &rq->cfs; cfs_rq; cfs_rq = NULL)

static inline int
is_same_group(struct sched_entity *se, struct sched_entity *pse)
{
	return 1;
}

static inline struct sched_entity *parent_entity(struct sched_entity *se)
{
	return NULL;
}

static inline void
find_matching_se(struct sched_entity **se, struct sched_entity **pse)
{
}

#endif	/* CONFIG_FAIR_GROUP_SCHED */


/**************************************************************
 * Scheduling class tree data structure manipulation methods:
 */

static inline u64 max_vruntime(u64 min_vruntime, u64 vruntime)
{
	s64 delta = (s64)(vruntime - min_vruntime);
	if (delta > 0)
		min_vruntime = vruntime;

	return min_vruntime;
}

static inline u64 min_vruntime(u64 min_vruntime, u64 vruntime)
{
	s64 delta = (s64)(vruntime - min_vruntime);
	if (delta < 0)
		min_vruntime = vruntime;

	return min_vruntime;
}

static inline int entity_before(struct sched_entity *a,
				struct sched_entity *b)
{
	return (s64)(a->vruntime - b->vruntime) < 0;
}

static inline s64 entity_key(struct cfs_rq *cfs_rq, struct sched_entity *se)
{
	return se->vruntime - cfs_rq->min_vruntime;
}

static void update_min_vruntime(struct cfs_rq *cfs_rq)
{
	u64 vruntime = cfs_rq->min_vruntime;

	if (cfs_rq->curr)
		vruntime = cfs_rq->curr->vruntime;

	if (cfs_rq->rb_leftmost) {
		struct sched_entity *se = rb_entry(cfs_rq->rb_leftmost,
						   struct sched_entity,
						   run_node);

		if (!cfs_rq->curr)
			vruntime = se->vruntime;
		else
			vruntime = min_vruntime(vruntime, se->vruntime);
	}

	cfs_rq->min_vruntime = max_vruntime(cfs_rq->min_vruntime, vruntime);
}

/*
 * Enqueue an entity into the rb-tree:
 */
static void __enqueue_entity(struct cfs_rq *cfs_rq, struct sched_entity *se)
{
	struct rb_node **link = &cfs_rq->tasks_timeline.rb_node;
	struct rb_node *parent = NULL;
	struct sched_entity *entry;
	s64 key = entity_key(cfs_rq, se);
	int leftmost = 1;

	/*
	 * Find the right place in the rbtree:
	 */
	while (*link) {
		parent = *link;
		entry = rb_entry(parent, struct sched_entity, run_node);
		/*
		 * We dont care about collisions. Nodes with
		 * the same key stay together.
		 */
		if (key < entity_key(cfs_rq, entry)) {
			link = &parent->rb_left;
		} else {
			link = &parent->rb_right;
			leftmost = 0;
		}
	}

	/*
	 * Maintain a cache of leftmost tree entries (it is frequently
	 * used):
	 */
	if (leftmost)
		cfs_rq->rb_leftmost = &se->run_node;

	rb_link_node(&se->run_node, parent, link);
	rb_insert_color(&se->run_node, &cfs_rq->tasks_timeline);
}

static void __dequeue_entity(struct cfs_rq *cfs_rq, struct sched_entity *se)
{
	if (cfs_rq->rb_leftmost == &se->run_node) {
		struct rb_node *next_node;

		next_node = rb_next(&se->run_node);
		cfs_rq->rb_leftmost = next_node;
	}

	rb_erase(&se->run_node, &cfs_rq->tasks_timeline);
}

static struct sched_entity *__pick_next_entity(struct cfs_rq *cfs_rq)
{
	struct rb_node *left = cfs_rq->rb_leftmost;

	if (!left)
		return NULL;

	return rb_entry(left, struct sched_entity, run_node);
}

static struct sched_entity *__pick_last_entity(struct cfs_rq *cfs_rq)
{
	struct rb_node *last = rb_last(&cfs_rq->tasks_timeline);

	if (!last)
		return NULL;

	return rb_entry(last, struct sched_entity, run_node);
}

/**************************************************************
 * Scheduling class statistics methods:
 */

#ifdef CONFIG_SCHED_DEBUG
int sched_nr_latency_handler(struct ctl_table *table, int write,
		struct file *filp, void __user *buffer, size_t *lenp,
		loff_t *ppos)
{
	int ret = proc_dointvec_minmax(table, write, filp, buffer, lenp, ppos);

	if (ret || !write)
		return ret;

	sched_nr_latency = DIV_ROUND_UP(sysctl_sched_latency,
					sysctl_sched_min_granularity);

	return 0;
}
#endif

/*
 * delta /= w
 */
static inline unsigned long
calc_delta_fair(unsigned long delta, struct sched_entity *se)
{
	if (unlikely(se->load.weight != NICE_0_LOAD))
		delta = calc_delta_mine(delta, NICE_0_LOAD, &se->load);

	return delta;
}

/*
 * The idea is to set a period in which each task runs once.
 *
 * When there are too many tasks (sysctl_sched_nr_latency) we have to stretch
 * this period because otherwise the slices get too small.
 *
 * p = (nr <= nl) ? l : l*nr/nl
 */
static u64 __sched_period(unsigned long nr_running)
{
	u64 period = sysctl_sched_latency;
	unsigned long nr_latency = sched_nr_latency;

	if (unlikely(nr_running > nr_latency)) {
		period = sysctl_sched_min_granularity;
		period *= nr_running;
	}

	return period;
}

/*
 * We calculate the wall-time slice from the period by taking a part
 * proportional to the weight.
 *
 * s = p*P[w/rw]
 */
static u64 sched_slice(struct cfs_rq *cfs_rq, struct sched_entity *se)
{
	u64 slice = __sched_period(cfs_rq->nr_running + !se->on_rq);

	for_each_sched_entity(se) {
		struct load_weight *load;
		struct load_weight lw;

		cfs_rq = cfs_rq_of(se);
		load = &cfs_rq->load;

		if (unlikely(!se->on_rq)) {
			lw = cfs_rq->load;

			update_load_add(&lw, se->load.weight);
			load = &lw;
		}
		slice = calc_delta_mine(slice, se->load.weight, load);
	}
	return slice;
}

/*
 * We calculate the vruntime slice of a to be inserted task
 *
 * vs = s/w
 */
static u64 sched_vslice(struct cfs_rq *cfs_rq, struct sched_entity *se)
{
	return calc_delta_fair(sched_slice(cfs_rq, se), se);
}

/*
 * Update the current task's runtime statistics. Skip current tasks that
 * are not in our scheduling class.
 */
static inline void
__update_curr(struct cfs_rq *cfs_rq, struct sched_entity *curr,
	      unsigned long delta_exec)
{
	unsigned long delta_exec_weighted;

	schedstat_set(curr->exec_max, max((u64)delta_exec, curr->exec_max));

	curr->sum_exec_runtime += delta_exec;
	schedstat_add(cfs_rq, exec_clock, delta_exec);
	delta_exec_weighted = calc_delta_fair(delta_exec, curr);
	curr->vruntime += delta_exec_weighted;
	update_min_vruntime(cfs_rq);
}

static void update_curr(struct cfs_rq *cfs_rq)
{
	struct sched_entity *curr = cfs_rq->curr;
	u64 now = rq_of(cfs_rq)->clock;
	unsigned long delta_exec;

	if (unlikely(!curr))
		return;

	/*
	 * Get the amount of time the current task was running
	 * since the last time we changed load (this cannot
	 * overflow on 32 bits):
	 */
	delta_exec = (unsigned long)(now - curr->exec_start);
	if (!delta_exec)
		return;

	__update_curr(cfs_rq, curr, delta_exec);
	curr->exec_start = now;

	if (entity_is_task(curr)) {
		struct task_struct *curtask = task_of(curr);

		cpuacct_charge(curtask, delta_exec);
		account_group_exec_runtime(curtask, delta_exec);
	}
}

static inline void
update_stats_wait_start(struct cfs_rq *cfs_rq, struct sched_entity *se)
{
	schedstat_set(se->wait_start, rq_of(cfs_rq)->clock);
}

/*
 * Task is being enqueued - update stats:
 */
static void update_stats_enqueue(struct cfs_rq *cfs_rq, struct sched_entity *se)
{
	/*
	 * Are we enqueueing a waiting task? (for current tasks
	 * a dequeue/enqueue event is a NOP)
	 */
	if (se != cfs_rq->curr)
		update_stats_wait_start(cfs_rq, se);
}

static void
update_stats_wait_end(struct cfs_rq *cfs_rq, struct sched_entity *se)
{
	schedstat_set(se->wait_max, max(se->wait_max,
			rq_of(cfs_rq)->clock - se->wait_start));
	schedstat_set(se->wait_count, se->wait_count + 1);
	schedstat_set(se->wait_sum, se->wait_sum +
			rq_of(cfs_rq)->clock - se->wait_start);
	schedstat_set(se->wait_start, 0);
}

static inline void
update_stats_dequeue(struct cfs_rq *cfs_rq, struct sched_entity *se)
{
	/*
	 * Mark the end of the wait period if dequeueing a
	 * waiting task:
	 */
	if (se != cfs_rq->curr)
		update_stats_wait_end(cfs_rq, se);
}

/*
 * We are picking a new current task - update its stats:
 */
static inline void
update_stats_curr_start(struct cfs_rq *cfs_rq, struct sched_entity *se)
{
	/*
	 * We are starting a new run period:
	 */
	se->exec_start = rq_of(cfs_rq)->clock;
}

/**************************************************
 * Scheduling class queueing methods:
 */

#if defined CONFIG_SMP && defined CONFIG_FAIR_GROUP_SCHED
static void
add_cfs_task_weight(struct cfs_rq *cfs_rq, unsigned long weight)
{
	cfs_rq->task_weight += weight;
}
#else
static inline void
add_cfs_task_weight(struct cfs_rq *cfs_rq, unsigned long weight)
{
}
#endif

static void
account_entity_enqueue(struct cfs_rq *cfs_rq, struct sched_entity *se)
{
	update_load_add(&cfs_rq->load, se->load.weight);
	if (!parent_entity(se))
		inc_cpu_load(rq_of(cfs_rq), se->load.weight);
	if (entity_is_task(se)) {
		add_cfs_task_weight(cfs_rq, se->load.weight);
		list_add(&se->group_node, &cfs_rq->tasks);
	}
	cfs_rq->nr_running++;
	se->on_rq = 1;
}

static void
account_entity_dequeue(struct cfs_rq *cfs_rq, struct sched_entity *se)
{
	update_load_sub(&cfs_rq->load, se->load.weight);
	if (!parent_entity(se))
		dec_cpu_load(rq_of(cfs_rq), se->load.weight);
	if (entity_is_task(se)) {
		add_cfs_task_weight(cfs_rq, -se->load.weight);
		list_del_init(&se->group_node);
	}
	cfs_rq->nr_running--;
	se->on_rq = 0;
}

static void enqueue_sleeper(struct cfs_rq *cfs_rq, struct sched_entity *se)
{
#ifdef CONFIG_SCHEDSTATS
	struct task_struct *tsk = NULL;

	if (entity_is_task(se))
		tsk = task_of(se);

	if (se->sleep_start) {
		u64 delta = rq_of(cfs_rq)->clock - se->sleep_start;

		if ((s64)delta < 0)
			delta = 0;

		if (unlikely(delta > se->sleep_max))
			se->sleep_max = delta;

		se->sleep_start = 0;
		se->sum_sleep_runtime += delta;

		if (tsk)
			account_scheduler_latency(tsk, delta >> 10, 1);
	}
	if (se->block_start) {
		u64 delta = rq_of(cfs_rq)->clock - se->block_start;

		if ((s64)delta < 0)
			delta = 0;

		if (unlikely(delta > se->block_max))
			se->block_max = delta;

		se->block_start = 0;
		se->sum_sleep_runtime += delta;

		if (tsk) {
			/*
			 * Blocking time is in units of nanosecs, so shift by
			 * 20 to get a milliseconds-range estimation of the
			 * amount of time that the task spent sleeping:
			 */
			if (unlikely(prof_on == SLEEP_PROFILING)) {
				profile_hits(SLEEP_PROFILING,
						(void *)get_wchan(tsk),
						delta >> 20);
			}
			account_scheduler_latency(tsk, delta >> 10, 0);
		}
	}
#endif
}

static void check_spread(struct cfs_rq *cfs_rq, struct sched_entity *se)
{
#ifdef CONFIG_SCHED_DEBUG
	s64 d = se->vruntime - cfs_rq->min_vruntime;

	if (d < 0)
		d = -d;

	if (d > 3*sysctl_sched_latency)
		schedstat_inc(cfs_rq, nr_spread_over);
#endif
}

static void
place_entity(struct cfs_rq *cfs_rq, struct sched_entity *se, int initial)
{
	u64 vruntime = cfs_rq->min_vruntime;

	/*
	 * The 'current' period is already promised to the current tasks,
	 * however the extra weight of the new task will slow them down a
	 * little, place the new task so that it fits in the slot that
	 * stays open at the end.
	 */
	if (initial && sched_feat(START_DEBIT))
		vruntime += sched_vslice(cfs_rq, se);

	if (!initial) {
		/* sleeps upto a single latency don't count. */
		if (sched_feat(NEW_FAIR_SLEEPERS)) {
			unsigned long thresh = sysctl_sched_latency;

			/*
			 * Convert the sleeper threshold into virtual time.
			 * SCHED_IDLE is a special sub-class.  We care about
			 * fairness only relative to other SCHED_IDLE tasks,
			 * all of which have the same weight.
			 */
			if (sched_feat(NORMALIZED_SLEEPER) &&
					(!entity_is_task(se) ||
					 task_of(se)->policy != SCHED_IDLE))
				thresh = calc_delta_fair(thresh, se);

			vruntime -= thresh;
		}

		/* ensure we never gain time by being placed backwards. */
		vruntime = max_vruntime(se->vruntime, vruntime);
	}

	se->vruntime = vruntime;
}

static void
enqueue_entity(struct cfs_rq *cfs_rq, struct sched_entity *se, int wakeup)
{
	/*
	 * Update run-time statistics of the 'current'.
	 */
	update_curr(cfs_rq);
	account_entity_enqueue(cfs_rq, se);

	if (wakeup) {
		place_entity(cfs_rq, se, 0);
		enqueue_sleeper(cfs_rq, se);
	}

	update_stats_enqueue(cfs_rq, se);
	check_spread(cfs_rq, se);
	if (se != cfs_rq->curr)
		__enqueue_entity(cfs_rq, se);
}

static void __clear_buddies(struct cfs_rq *cfs_rq, struct sched_entity *se)
{
	if (cfs_rq->last == se)
		cfs_rq->last = NULL;

	if (cfs_rq->next == se)
		cfs_rq->next = NULL;
}

static void clear_buddies(struct cfs_rq *cfs_rq, struct sched_entity *se)
{
	for_each_sched_entity(se)
		__clear_buddies(cfs_rq_of(se), se);
}

static void
dequeue_entity(struct cfs_rq *cfs_rq, struct sched_entity *se, int sleep)
{
	/*
	 * Update run-time statistics of the 'current'.
	 */
	update_curr(cfs_rq);

	update_stats_dequeue(cfs_rq, se);
	if (sleep) {
#ifdef CONFIG_SCHEDSTATS
		if (entity_is_task(se)) {
			struct task_struct *tsk = task_of(se);

			if (tsk->state & TASK_INTERRUPTIBLE)
				se->sleep_start = rq_of(cfs_rq)->clock;
			if (tsk->state & TASK_UNINTERRUPTIBLE)
				se->block_start = rq_of(cfs_rq)->clock;
		}
#endif
	}

	clear_buddies(cfs_rq, se);

	if (se != cfs_rq->curr)
		__dequeue_entity(cfs_rq, se);
	account_entity_dequeue(cfs_rq, se);
	update_min_vruntime(cfs_rq);
}

/*
 * Preempt the current task with a newly woken task if needed:
 */
static void
check_preempt_tick(struct cfs_rq *cfs_rq, struct sched_entity *curr)
{
	unsigned long ideal_runtime, delta_exec;

	ideal_runtime = sched_slice(cfs_rq, curr);
	delta_exec = curr->sum_exec_runtime - curr->prev_sum_exec_runtime;
	if (delta_exec > ideal_runtime) {
		resched_task(rq_of(cfs_rq)->curr);
		/*
		 * The current task ran long enough, ensure it doesn't get
		 * re-elected due to buddy favours.
		 */
		clear_buddies(cfs_rq, curr);
	}
}

static void
set_next_entity(struct cfs_rq *cfs_rq, struct sched_entity *se)
{
	/* 'current' is not kept within the tree. */
	if (se->on_rq) {
		/*
		 * Any task has to be enqueued before it get to execute on
		 * a CPU. So account for the time it spent waiting on the
		 * runqueue.
		 */
		update_stats_wait_end(cfs_rq, se);
		__dequeue_entity(cfs_rq, se);
	}

	update_stats_curr_start(cfs_rq, se);
	cfs_rq->curr = se;
#ifdef CONFIG_SCHEDSTATS
	/*
	 * Track our maximum slice length, if the CPU's load is at
	 * least twice that of our own weight (i.e. dont track it
	 * when there are only lesser-weight tasks around):
	 */
	if (rq_of(cfs_rq)->load.weight >= 2*se->load.weight) {
		se->slice_max = max(se->slice_max,
			se->sum_exec_runtime - se->prev_sum_exec_runtime);
	}
#endif
	se->prev_sum_exec_runtime = se->sum_exec_runtime;
}

static int
wakeup_preempt_entity(struct sched_entity *curr, struct sched_entity *se);

static struct sched_entity *pick_next_entity(struct cfs_rq *cfs_rq)
{
	struct sched_entity *se = __pick_next_entity(cfs_rq);

	if (cfs_rq->next && wakeup_preempt_entity(cfs_rq->next, se) < 1)
		return cfs_rq->next;

	if (cfs_rq->last && wakeup_preempt_entity(cfs_rq->last, se) < 1)
		return cfs_rq->last;

	return se;
}

static void put_prev_entity(struct cfs_rq *cfs_rq, struct sched_entity *prev)
{
	/*
	 * If still on the runqueue then deactivate_task()
	 * was not called and update_curr() has to be done:
	 */
	if (prev->on_rq)
		update_curr(cfs_rq);

	check_spread(cfs_rq, prev);
	if (prev->on_rq) {
		update_stats_wait_start(cfs_rq, prev);
		/* Put 'current' back into the tree. */
		__enqueue_entity(cfs_rq, prev);
	}
	cfs_rq->curr = NULL;
}

static void
entity_tick(struct cfs_rq *cfs_rq, struct sched_entity *curr, int queued)
{
	/*
	 * Update run-time statistics of the 'current'.
	 */
	update_curr(cfs_rq);

#ifdef CONFIG_SCHED_HRTICK
	/*
	 * queued ticks are scheduled to match the slice, so don't bother
	 * validating it and just reschedule.
	 */
	if (queued) {
		resched_task(rq_of(cfs_rq)->curr);
		return;
	}
	/*
	 * don't let the period tick interfere with the hrtick preemption
	 */
	if (!sched_feat(DOUBLE_TICK) &&
			hrtimer_active(&rq_of(cfs_rq)->hrtick_timer))
		return;
#endif

	if (cfs_rq->nr_running > 1 || !sched_feat(WAKEUP_PREEMPT))
		check_preempt_tick(cfs_rq, curr);
}

/**************************************************
 * CFS operations on tasks:
 */

#ifdef CONFIG_SCHED_HRTICK
static void hrtick_start_fair(struct rq *rq, struct task_struct *p)
{
	struct sched_entity *se = &p->se;
	struct cfs_rq *cfs_rq = cfs_rq_of(se);

	WARN_ON(task_rq(p) != rq);

	if (hrtick_enabled(rq) && cfs_rq->nr_running > 1) {
		u64 slice = sched_slice(cfs_rq, se);
		u64 ran = se->sum_exec_runtime - se->prev_sum_exec_runtime;
		s64 delta = slice - ran;

		if (delta < 0) {
			if (rq->curr == p)
				resched_task(p);
			return;
		}

		/*
		 * Don't schedule slices shorter than 10000ns, that just
		 * doesn't make sense. Rely on vruntime for fairness.
		 */
		if (rq->curr != p)
			delta = max_t(s64, 10000LL, delta);

		hrtick_start(rq, delta);
	}
}

/*
 * called from enqueue/dequeue and updates the hrtick when the
 * current task is from our class and nr_running is low enough
 * to matter.
 */
static void hrtick_update(struct rq *rq)
{
	struct task_struct *curr = rq->curr;

	if (curr->sched_class != &fair_sched_class)
		return;

	if (cfs_rq_of(&curr->se)->nr_running < sched_nr_latency)
		hrtick_start_fair(rq, curr);
}
#else /* !CONFIG_SCHED_HRTICK */
static inline void
hrtick_start_fair(struct rq *rq, struct task_struct *p)
{
}

static inline void hrtick_update(struct rq *rq)
{
}
#endif

/*
 * The enqueue_task method is called before nr_running is
 * increased. Here we update the fair scheduling stats and
 * then put the task into the rbtree:
 */
static void enqueue_task_fair(struct rq *rq, struct task_struct *p, int wakeup)
{
	struct cfs_rq *cfs_rq;
	struct sched_entity *se = &p->se;

	for_each_sched_entity(se) {
		if (se->on_rq)
			break;
		cfs_rq = cfs_rq_of(se);
		enqueue_entity(cfs_rq, se, wakeup);
		wakeup = 1;
	}

	hrtick_update(rq);
}

/*
 * The dequeue_task method is called before nr_running is
 * decreased. We remove the task from the rbtree and
 * update the fair scheduling stats:
 */
static void dequeue_task_fair(struct rq *rq, struct task_struct *p, int sleep)
{
	struct cfs_rq *cfs_rq;
	struct sched_entity *se = &p->se;

	for_each_sched_entity(se) {
		cfs_rq = cfs_rq_of(se);
		dequeue_entity(cfs_rq, se, sleep);
		/* Don't dequeue parent if it has other entities besides us */
		if (cfs_rq->load.weight)
			break;
		sleep = 1;
	}

	hrtick_update(rq);
}

/*
 * sched_yield() support is very simple - we dequeue and enqueue.
 *
 * If compat_yield is turned on then we requeue to the end of the tree.
 */
static void yield_task_fair(struct rq *rq)
{
	struct task_struct *curr = rq->curr;
	struct cfs_rq *cfs_rq = task_cfs_rq(curr);
	struct sched_entity *rightmost, *se = &curr->se;

	/*
	 * Are we the only task in the tree?
	 */
	if (unlikely(cfs_rq->nr_running == 1))
		return;

	clear_buddies(cfs_rq, se);

	if (likely(!sysctl_sched_compat_yield) && curr->policy != SCHED_BATCH) {
		update_rq_clock(rq);
		/*
		 * Update run-time statistics of the 'current'.
		 */
		update_curr(cfs_rq);

		return;
	}
	/*
	 * Find the rightmost entry in the rbtree:
	 */
	rightmost = __pick_last_entity(cfs_rq);
	/*
	 * Already in the rightmost position?
	 */
	if (unlikely(!rightmost || entity_before(rightmost, se)))
		return;

	/*
	 * Minimally necessary key value to be last in the tree:
	 * Upon rescheduling, sched_class::put_prev_task() will place
	 * 'current' within the tree based on its new key value.
	 */
	se->vruntime = rightmost->vruntime + 1;
}

/*
 * wake_idle() will wake a task on an idle cpu if task->cpu is
 * not idle and an idle cpu is available.  The span of cpus to
 * search starts with cpus closest then further out as needed,
 * so we always favor a closer, idle cpu.
 * Domains may include CPUs that are not usable for migration,
 * hence we need to mask them out (cpu_active_mask)
 *
 * Returns the CPU we should wake onto.
 */
#if defined(ARCH_HAS_SCHED_WAKE_IDLE)
static int wake_idle(int cpu, struct task_struct *p)
{
	struct sched_domain *sd;
	int i;
	unsigned int chosen_wakeup_cpu;
	int this_cpu;

	/*
	 * At POWERSAVINGS_BALANCE_WAKEUP level, if both this_cpu and prev_cpu
	 * are idle and this is not a kernel thread and this task's affinity
	 * allows it to be moved to preferred cpu, then just move!
	 */

	this_cpu = smp_processor_id();
	chosen_wakeup_cpu =
		cpu_rq(this_cpu)->rd->sched_mc_preferred_wakeup_cpu;

	if (sched_mc_power_savings >= POWERSAVINGS_BALANCE_WAKEUP &&
		idle_cpu(cpu) && idle_cpu(this_cpu) &&
		p->mm && !(p->flags & PF_KTHREAD) &&
		cpu_isset(chosen_wakeup_cpu, p->cpus_allowed))
		return chosen_wakeup_cpu;

	/*
	 * If it is idle, then it is the best cpu to run this task.
	 *
	 * This cpu is also the best, if it has more than one task already.
	 * Siblings must be also busy(in most cases) as they didn't already
	 * pickup the extra load from this cpu and hence we need not check
	 * sibling runqueue info. This will avoid the checks and cache miss
	 * penalities associated with that.
	 */
	if (idle_cpu(cpu) || cpu_rq(cpu)->cfs.nr_running > 1)
		return cpu;

	for_each_domain(cpu, sd) {
		if ((sd->flags & SD_WAKE_IDLE)
		    || ((sd->flags & SD_WAKE_IDLE_FAR)
			&& !task_hot(p, task_rq(p)->clock, sd))) {
			for_each_cpu_and(i, sched_domain_span(sd),
					 &p->cpus_allowed) {
				if (cpu_active(i) && idle_cpu(i)) {
					if (i != task_cpu(p)) {
						schedstat_inc(p,
						       se.nr_wakeups_idle);
					}
					return i;
				}
			}
		} else {
			break;
		}
	}
	return cpu;
}
#else /* !ARCH_HAS_SCHED_WAKE_IDLE*/
static inline int wake_idle(int cpu, struct task_struct *p)
{
	return cpu;
}
#endif

#ifdef CONFIG_SMP

#ifdef CONFIG_FAIR_GROUP_SCHED
/*
 * effective_load() calculates the load change as seen from the root_task_group
 *
 * Adding load to a group doesn't make a group heavier, but can cause movement
 * of group shares between cpus. Assuming the shares were perfectly aligned one
 * can calculate the shift in shares.
 *
 * The problem is that perfectly aligning the shares is rather expensive, hence
 * we try to avoid doing that too often - see update_shares(), which ratelimits
 * this change.
 *
 * We compensate this by not only taking the current delta into account, but
 * also considering the delta between when the shares were last adjusted and
 * now.
 *
 * We still saw a performance dip, some tracing learned us that between
 * cgroup:/ and cgroup:/foo balancing the number of affine wakeups increased
 * significantly. Therefore try to bias the error in direction of failing
 * the affine wakeup.
 *
 */
static long effective_load(struct task_group *tg, int cpu,
		long wl, long wg)
{
	struct sched_entity *se = tg->se[cpu];

	if (!tg->parent)
		return wl;

	/*
	 * By not taking the decrease of shares on the other cpu into
	 * account our error leans towards reducing the affine wakeups.
	 */
	if (!wl && sched_feat(ASYM_EFF_LOAD))
		return wl;

	for_each_sched_entity(se) {
		long S, rw, s, a, b;
		long more_w;

		/*
		 * Instead of using this increment, also add the difference
		 * between when the shares were last updated and now.
		 */
		more_w = se->my_q->load.weight - se->my_q->rq_weight;
		wl += more_w;
		wg += more_w;

		S = se->my_q->tg->shares;
		s = se->my_q->shares;
		rw = se->my_q->rq_weight;

		a = S*(rw + wl);
		b = S*rw + s*wg;

		wl = s*(a-b);

		if (likely(b))
			wl /= b;

		/*
		 * Assume the group is already running and will
		 * thus already be accounted for in the weight.
		 *
		 * That is, moving shares between CPUs, does not
		 * alter the group weight.
		 */
		wg = 0;
	}

	return wl;
}

#else

static inline unsigned long effective_load(struct task_group *tg, int cpu,
		unsigned long wl, unsigned long wg)
{
	return wl;
}

#endif

static int
wake_affine(struct sched_domain *this_sd, struct rq *this_rq,
	    struct task_struct *p, int prev_cpu, int this_cpu, int sync,
	    int idx, unsigned long load, unsigned long this_load,
	    unsigned int imbalance)
{
	struct task_struct *curr = this_rq->curr;
	struct task_group *tg;
	unsigned long tl = this_load;
	unsigned long tl_per_task;
	unsigned long weight;
	int balanced;

	if (!(this_sd->flags & SD_WAKE_AFFINE) || !sched_feat(AFFINE_WAKEUPS))
		return 0;

	if (sync && (curr->se.avg_overlap > sysctl_sched_migration_cost ||
			p->se.avg_overlap > sysctl_sched_migration_cost))
		sync = 0;

	/*
	 * If sync wakeup then subtract the (maximum possible)
	 * effect of the currently running task from the load
	 * of the current CPU:
	 */
	if (sync) {
		tg = task_group(current);
		weight = current->se.load.weight;

		tl += effective_load(tg, this_cpu, -weight, -weight);
		load += effective_load(tg, prev_cpu, 0, -weight);
	}

	tg = task_group(p);
	weight = p->se.load.weight;

	balanced = 100*(tl + effective_load(tg, this_cpu, weight, weight)) <=
		imbalance*(load + effective_load(tg, prev_cpu, 0, weight));

	/*
	 * If the currently running task will sleep within
	 * a reasonable amount of time then attract this newly
	 * woken task:
	 */
	if (sync && balanced)
		return 1;

	schedstat_inc(p, se.nr_wakeups_affine_attempts);
	tl_per_task = cpu_avg_load_per_task(this_cpu);

	if (balanced || (tl <= load && tl + target_load(prev_cpu, idx) <=
			tl_per_task)) {
		/*
		 * This domain has SD_WAKE_AFFINE and
		 * p is cache cold in this domain, and
		 * there is no bad imbalance.
		 */
		schedstat_inc(this_sd, ttwu_move_affine);
		schedstat_inc(p, se.nr_wakeups_affine);

		return 1;
	}
	return 0;
}

static int select_task_rq_fair(struct task_struct *p, int sync)
{
	struct sched_domain *sd, *this_sd = NULL;
	int prev_cpu, this_cpu, new_cpu;
	unsigned long load, this_load;
	struct rq *this_rq;
	unsigned int imbalance;
	int idx;

	prev_cpu	= task_cpu(p);
	this_cpu	= smp_processor_id();
	this_rq		= cpu_rq(this_cpu);
	new_cpu		= prev_cpu;

	if (prev_cpu == this_cpu)
		goto out;
	/*
	 * 'this_sd' is the first domain that both
	 * this_cpu and prev_cpu are present in:
	 */
	for_each_domain(this_cpu, sd) {
		if (cpumask_test_cpu(prev_cpu, sched_domain_span(sd))) {
			this_sd = sd;
			break;
		}
	}

	if (unlikely(!cpumask_test_cpu(this_cpu, &p->cpus_allowed)))
		goto out;

	/*
	 * Check for affine wakeup and passive balancing possibilities.
	 */
	if (!this_sd)
		goto out;

	idx = this_sd->wake_idx;

	imbalance = 100 + (this_sd->imbalance_pct - 100) / 2;

	load = source_load(prev_cpu, idx);
	this_load = target_load(this_cpu, idx);

	if (wake_affine(this_sd, this_rq, p, prev_cpu, this_cpu, sync, idx,
				     load, this_load, imbalance))
		return this_cpu;

	/*
	 * Start passive balancing when half the imbalance_pct
	 * limit is reached.
	 */
	if (this_sd->flags & SD_WAKE_BALANCE) {
		if (imbalance*this_load <= 100*load) {
			schedstat_inc(this_sd, ttwu_move_balance);
			schedstat_inc(p, se.nr_wakeups_passive);
			return this_cpu;
		}
	}

out:
	return wake_idle(new_cpu, p);
}
#endif /* CONFIG_SMP */

/*
 * Adaptive granularity
 *
 * se->avg_wakeup gives the average time a task runs until it does a wakeup,
 * with the limit of wakeup_gran -- when it never does a wakeup.
 *
 * So the smaller avg_wakeup is the faster we want this task to preempt,
 * but we don't want to treat the preemptee unfairly and therefore allow it
 * to run for at least the amount of time we'd like to run.
 *
 * NOTE: we use 2*avg_wakeup to increase the probability of actually doing one
 *
 * NOTE: we use *nr_running to scale with load, this nicely matches the
 *       degrading latency on load.
 */
static unsigned long
adaptive_gran(struct sched_entity *curr, struct sched_entity *se)
{
	u64 this_run = curr->sum_exec_runtime - curr->prev_sum_exec_runtime;
	u64 expected_wakeup = 2*se->avg_wakeup * cfs_rq_of(se)->nr_running;
	u64 gran = 0;

	if (this_run < expected_wakeup)
		gran = expected_wakeup - this_run;

	return min_t(s64, gran, sysctl_sched_wakeup_granularity);
}

static unsigned long
wakeup_gran(struct sched_entity *curr, struct sched_entity *se)
{
	unsigned long gran = sysctl_sched_wakeup_granularity;

	if (cfs_rq_of(curr)->curr && sched_feat(ADAPTIVE_GRAN))
		gran = adaptive_gran(curr, se);

	/*
	 * Since its curr running now, convert the gran from real-time
	 * to virtual-time in his units.
	 */
	if (sched_feat(ASYM_GRAN)) {
		/*
		 * By using 'se' instead of 'curr' we penalize light tasks, so
		 * they get preempted easier. That is, if 'se' < 'curr' then
		 * the resulting gran will be larger, therefore penalizing the
		 * lighter, if otoh 'se' > 'curr' then the resulting gran will
		 * be smaller, again penalizing the lighter task.
		 *
		 * This is especially important for buddies when the leftmost
		 * task is higher priority than the buddy.
		 */
		if (unlikely(se->load.weight != NICE_0_LOAD))
			gran = calc_delta_fair(gran, se);
	} else {
		if (unlikely(curr->load.weight != NICE_0_LOAD))
			gran = calc_delta_fair(gran, curr);
	}

	return gran;
}

/*
 * Should 'se' preempt 'curr'.
 *
 *             |s1
 *        |s2
 *   |s3
 *         g
 *      |<--->|c
 *
 *  w(c, s1) = -1
 *  w(c, s2) =  0
 *  w(c, s3) =  1
 *
 */
static int
wakeup_preempt_entity(struct sched_entity *curr, struct sched_entity *se)
{
	s64 gran, vdiff = curr->vruntime - se->vruntime;

	if (vdiff <= 0)
		return -1;

	gran = wakeup_gran(curr, se);
	if (vdiff > gran)
		return 1;

	return 0;
}

static void set_last_buddy(struct sched_entity *se)
{
	if (likely(task_of(se)->policy != SCHED_IDLE)) {
		for_each_sched_entity(se)
			cfs_rq_of(se)->last = se;
	}
}

static void set_next_buddy(struct sched_entity *se)
{
	if (likely(task_of(se)->policy != SCHED_IDLE)) {
		for_each_sched_entity(se)
			cfs_rq_of(se)->next = se;
	}
}

/*
 * Preempt the current task with a newly woken task if needed:
 */
static void check_preempt_wakeup(struct rq *rq, struct task_struct *p, int sync)
{
	struct task_struct *curr = rq->curr;
	struct sched_entity *se = &curr->se, *pse = &p->se;
	struct cfs_rq *cfs_rq = task_cfs_rq(curr);

	update_curr(cfs_rq);

	if (unlikely(rt_prio(p->prio))) {
		resched_task(curr);
		return;
	}

	if (unlikely(p->sched_class != &fair_sched_class))
		return;

	if (unlikely(se == pse))
		return;

	/*
	 * Only set the backward buddy when the current task is still on the
	 * rq. This can happen when a wakeup gets interleaved with schedule on
	 * the ->pre_schedule() or idle_balance() point, either of which can
	 * drop the rq lock.
	 *
	 * Also, during early boot the idle thread is in the fair class, for
	 * obvious reasons its a bad idea to schedule back to the idle thread.
	 */
	if (sched_feat(LAST_BUDDY) && likely(se->on_rq && curr != rq->idle))
		set_last_buddy(se);
	set_next_buddy(pse);

	/*
	 * We can come here with TIF_NEED_RESCHED already set from new task
	 * wake up path.
	 */
	if (test_tsk_need_resched(curr))
		return;

	/*
	 * Batch and idle tasks do not preempt (their preemption is driven by
	 * the tick):
	 */
	if (unlikely(p->policy != SCHED_NORMAL))
		return;

	/* Idle tasks are by definition preempted by everybody. */
	if (unlikely(curr->policy == SCHED_IDLE)) {
		resched_task(curr);
		return;
	}

	if (!sched_feat(WAKEUP_PREEMPT))
		return;

	if (sched_feat(WAKEUP_OVERLAP) && (sync ||
			(se->avg_overlap < sysctl_sched_migration_cost &&
			 pse->avg_overlap < sysctl_sched_migration_cost))) {
		resched_task(curr);
		return;
	}

	find_matching_se(&se, &pse);

	BUG_ON(!pse);

	if (wakeup_preempt_entity(se, pse) == 1)
		resched_task(curr);
}

static struct task_struct *pick_next_task_fair(struct rq *rq)
{
	struct task_struct *p;
	struct cfs_rq *cfs_rq = &rq->cfs;
	struct sched_entity *se;

	if (unlikely(!cfs_rq->nr_running))
		return NULL;

	do {
		se = pick_next_entity(cfs_rq);
		/*
		 * If se was a buddy, clear it so that it will have to earn
		 * the favour again.
		 */
		__clear_buddies(cfs_rq, se);
		set_next_entity(cfs_rq, se);
		cfs_rq = group_cfs_rq(se);
	} while (cfs_rq);

	p = task_of(se);
	hrtick_start_fair(rq, p);

	return p;
}

/*
 * Account for a descheduled task:
 */
static void put_prev_task_fair(struct rq *rq, struct task_struct *prev)
{
	struct sched_entity *se = &prev->se;
	struct cfs_rq *cfs_rq;

	for_each_sched_entity(se) {
		cfs_rq = cfs_rq_of(se);
		put_prev_entity(cfs_rq, se);
	}
}

#ifdef CONFIG_SMP
/**************************************************
 * Fair scheduling class load-balancing methods:
 */

/*
 * Load-balancing iterator. Note: while the runqueue stays locked
 * during the whole iteration, the current task might be
 * dequeued so the iterator has to be dequeue-safe. Here we
 * achieve that by always pre-iterating before returning
 * the current task:
 */
static struct task_struct *
__load_balance_iterator(struct cfs_rq *cfs_rq, struct list_head *next)
{
	struct task_struct *p = NULL;
	struct sched_entity *se;

	if (next == &cfs_rq->tasks)
		return NULL;

	se = list_entry(next, struct sched_entity, group_node);
	p = task_of(se);
	cfs_rq->balance_iterator = next->next;

	return p;
}

static struct task_struct *load_balance_start_fair(void *arg)
{
	struct cfs_rq *cfs_rq = arg;

	return __load_balance_iterator(cfs_rq, cfs_rq->tasks.next);
}

static struct task_struct *load_balance_next_fair(void *arg)
{
	struct cfs_rq *cfs_rq = arg;

	return __load_balance_iterator(cfs_rq, cfs_rq->balance_iterator);
}

static unsigned long
__load_balance_fair(struct rq *this_rq, int this_cpu, struct rq *busiest,
		unsigned long max_load_move, struct sched_domain *sd,
		enum cpu_idle_type idle, int *all_pinned, int *this_best_prio,
		struct cfs_rq *cfs_rq)
{
	struct rq_iterator cfs_rq_iterator;

	cfs_rq_iterator.start = load_balance_start_fair;
	cfs_rq_iterator.next = load_balance_next_fair;
	cfs_rq_iterator.arg = cfs_rq;

	return balance_tasks(this_rq, this_cpu, busiest,
			max_load_move, sd, idle, all_pinned,
			this_best_prio, &cfs_rq_iterator);
}

#ifdef CONFIG_FAIR_GROUP_SCHED
static unsigned long
load_balance_fair(struct rq *this_rq, int this_cpu, struct rq *busiest,
		  unsigned long max_load_move,
		  struct sched_domain *sd, enum cpu_idle_type idle,
		  int *all_pinned, int *this_best_prio)
{
	long rem_load_move = max_load_move;
	int busiest_cpu = cpu_of(busiest);
	struct task_group *tg;

	rcu_read_lock();
	update_h_load(busiest_cpu);

	list_for_each_entry_rcu(tg, &task_groups, list) {
		struct cfs_rq *busiest_cfs_rq = tg->cfs_rq[busiest_cpu];
		unsigned long busiest_h_load = busiest_cfs_rq->h_load;
		unsigned long busiest_weight = busiest_cfs_rq->load.weight;
		u64 rem_load, moved_load;

		/*
		 * empty group
		 */
		if (!busiest_cfs_rq->task_weight)
			continue;

		rem_load = (u64)rem_load_move * busiest_weight;
		rem_load = div_u64(rem_load, busiest_h_load + 1);

		moved_load = __load_balance_fair(this_rq, this_cpu, busiest,
				rem_load, sd, idle, all_pinned, this_best_prio,
				tg->cfs_rq[busiest_cpu]);

		if (!moved_load)
			continue;

		moved_load *= busiest_h_load;
		moved_load = div_u64(moved_load, busiest_weight + 1);

		rem_load_move -= moved_load;
		if (rem_load_move < 0)
			break;
	}
	rcu_read_unlock();

	return max_load_move - rem_load_move;
}
#else
static unsigned long
load_balance_fair(struct rq *this_rq, int this_cpu, struct rq *busiest,
		  unsigned long max_load_move,
		  struct sched_domain *sd, enum cpu_idle_type idle,
		  int *all_pinned, int *this_best_prio)
{
	return __load_balance_fair(this_rq, this_cpu, busiest,
			max_load_move, sd, idle, all_pinned,
			this_best_prio, &busiest->cfs);
}
#endif

static int
move_one_task_fair(struct rq *this_rq, int this_cpu, struct rq *busiest,
		   struct sched_domain *sd, enum cpu_idle_type idle)
{
	struct cfs_rq *busy_cfs_rq;
	struct rq_iterator cfs_rq_iterator;

	cfs_rq_iterator.start = load_balance_start_fair;
	cfs_rq_iterator.next = load_balance_next_fair;

	for_each_leaf_cfs_rq(busiest, busy_cfs_rq) {
		/*
		 * pass busy_cfs_rq argument into
		 * load_balance_[start|next]_fair iterators
		 */
		cfs_rq_iterator.arg = busy_cfs_rq;
		if (iter_move_one_task(this_rq, this_cpu, busiest, sd, idle,
				       &cfs_rq_iterator))
		    return 1;
	}

	return 0;
}
#endif /* CONFIG_SMP */

/*
 * scheduler tick hitting a task of our scheduling class:
 */
static void task_tick_fair(struct rq *rq, struct task_struct *curr, int queued)
{
	struct cfs_rq *cfs_rq;
	struct sched_entity *se = &curr->se;

	for_each_sched_entity(se) {
		cfs_rq = cfs_rq_of(se);
		entity_tick(cfs_rq, se, queued);
	}
}

/*
 * Share the fairness runtime between parent and child, thus the
 * total amount of pressure for CPU stays equal - new tasks
 * get a chance to run but frequent forkers are not allowed to
 * monopolize the CPU. Note: the parent runqueue is locked,
 * the child is not running yet.
 */
static void task_new_fair(struct rq *rq, struct task_struct *p)
{
	struct cfs_rq *cfs_rq = task_cfs_rq(p);
	struct sched_entity *se = &p->se, *curr = cfs_rq->curr;
	int this_cpu = smp_processor_id();

	sched_info_queued(p);

	update_curr(cfs_rq);
	place_entity(cfs_rq, se, 1);

	/* 'curr' will be NULL if the child belongs to a different group */
	if (sysctl_sched_child_runs_first && this_cpu == task_cpu(p) &&
			curr && entity_before(curr, se)) {
		/*
		 * Upon rescheduling, sched_class::put_prev_task() will place
		 * 'current' within the tree based on its new key value.
		 */
		swap(curr->vruntime, se->vruntime);
		resched_task(rq->curr);
	}

	enqueue_task_fair(rq, p, 0);
}

/*
 * Priority of the task has changed. Check to see if we preempt
 * the current task.
 */
static void prio_changed_fair(struct rq *rq, struct task_struct *p,
			      int oldprio, int running)
{
	/*
	 * Reschedule if we are currently running on this runqueue and
	 * our priority decreased, or if we are not currently running on
	 * this runqueue and our priority is higher than the current's
	 */
	if (running) {
		if (p->prio > oldprio)
			resched_task(rq->curr);
	} else
		check_preempt_curr(rq, p, 0);
}

/*
 * We switched to the sched_fair class.
 */
static void switched_to_fair(struct rq *rq, struct task_struct *p,
			     int running)
{
	/*
	 * We were most likely switched from sched_rt, so
	 * kick off the schedule if running, otherwise just see
	 * if we can still preempt the current task.
	 */
	if (running)
		resched_task(rq->curr);
	else
		check_preempt_curr(rq, p, 0);
}

/* Account for a task changing its policy or group.
 *
 * This routine is mostly called to set cfs_rq->curr field when a task
 * migrates between groups/classes.
 */
static void set_curr_task_fair(struct rq *rq)
{
	struct sched_entity *se = &rq->curr->se;

	for_each_sched_entity(se)
		set_next_entity(cfs_rq_of(se), se);
}

#ifdef CONFIG_FAIR_GROUP_SCHED
static void moved_group_fair(struct task_struct *p)
{
	struct cfs_rq *cfs_rq = task_cfs_rq(p);

	update_curr(cfs_rq);
	place_entity(cfs_rq, &p->se, 1);
}
#endif

/*
 * All the scheduling class methods:
 */
static const struct sched_class fair_sched_class = {
	.next			= &idle_sched_class,
	.enqueue_task		= enqueue_task_fair,
	.dequeue_task		= dequeue_task_fair,
	.yield_task		= yield_task_fair,

	.check_preempt_curr	= check_preempt_wakeup,

	.pick_next_task		= pick_next_task_fair,
	.put_prev_task		= put_prev_task_fair,

#ifdef CONFIG_SMP
	.select_task_rq		= select_task_rq_fair,

	.load_balance		= load_balance_fair,
	.move_one_task		= move_one_task_fair,
#endif

	.set_curr_task          = set_curr_task_fair,
	.task_tick		= task_tick_fair,
	.task_new		= task_new_fair,

	.prio_changed		= prio_changed_fair,
	.switched_to		= switched_to_fair,

#ifdef CONFIG_FAIR_GROUP_SCHED
	.moved_group		= moved_group_fair,
#endif
};

#ifdef CONFIG_SCHED_DEBUG
static void print_cfs_stats(struct seq_file *m, int cpu)
{
	struct cfs_rq *cfs_rq;

	rcu_read_lock();
	for_each_leaf_cfs_rq(cpu_rq(cpu), cfs_rq)
		print_cfs_rq(m, cpu, cfs_rq);
	rcu_read_unlock();
}
#endif
