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
 */

/*
 * Preemption granularity:
 * (default: 2 msec, units: nanoseconds)
 *
 * NOTE: this granularity value is not the same as the concept of
 * 'timeslice length' - timeslices in CFS will typically be somewhat
 * larger than this value. (to see the precise effective timeslice
 * length of your workload, run vmstat and monitor the context-switches
 * field)
 *
 * On SMP systems the value of this is multiplied by the log2 of the
 * number of CPUs. (i.e. factor 2x on 2-way systems, 3x on 4-way
 * systems, 4x on 8-way systems, 5x on 16-way systems, etc.)
 */
unsigned int sysctl_sched_granularity __read_mostly = 2000000000ULL/HZ;

/*
 * SCHED_BATCH wake-up granularity.
 * (default: 10 msec, units: nanoseconds)
 *
 * This option delays the preemption effects of decoupled workloads
 * and reduces their over-scheduling. Synchronous workloads will still
 * have immediate wakeup/sleep latencies.
 */
unsigned int sysctl_sched_batch_wakeup_granularity __read_mostly =
							10000000000ULL/HZ;

/*
 * SCHED_OTHER wake-up granularity.
 * (default: 1 msec, units: nanoseconds)
 *
 * This option delays the preemption effects of decoupled workloads
 * and reduces their over-scheduling. Synchronous workloads will still
 * have immediate wakeup/sleep latencies.
 */
unsigned int sysctl_sched_wakeup_granularity __read_mostly = 1000000000ULL/HZ;

unsigned int sysctl_sched_stat_granularity __read_mostly;

/*
 * Initialized in sched_init_granularity():
 */
unsigned int sysctl_sched_runtime_limit __read_mostly;

/*
 * Debugging: various feature bits
 */
enum {
	SCHED_FEAT_FAIR_SLEEPERS	= 1,
	SCHED_FEAT_SLEEPER_AVG		= 2,
	SCHED_FEAT_SLEEPER_LOAD_AVG	= 4,
	SCHED_FEAT_PRECISE_CPU_LOAD	= 8,
	SCHED_FEAT_START_DEBIT		= 16,
	SCHED_FEAT_SKIP_INITIAL		= 32,
};

unsigned int sysctl_sched_features __read_mostly =
		SCHED_FEAT_FAIR_SLEEPERS	*1 |
		SCHED_FEAT_SLEEPER_AVG		*1 |
		SCHED_FEAT_SLEEPER_LOAD_AVG	*1 |
		SCHED_FEAT_PRECISE_CPU_LOAD	*1 |
		SCHED_FEAT_START_DEBIT		*1 |
		SCHED_FEAT_SKIP_INITIAL		*0;

extern struct sched_class fair_sched_class;

/**************************************************************
 * CFS operations on generic schedulable entities:
 */

#ifdef CONFIG_FAIR_GROUP_SCHED

/* cpu runqueue to which this cfs_rq is attached */
static inline struct rq *rq_of(struct cfs_rq *cfs_rq)
{
	return cfs_rq->rq;
}

/* currently running entity (if any) on this cfs_rq */
static inline struct sched_entity *cfs_rq_curr(struct cfs_rq *cfs_rq)
{
	return cfs_rq->curr;
}

/* An entity is a task if it doesn't "own" a runqueue */
#define entity_is_task(se)	(!se->my_q)

static inline void
set_cfs_rq_curr(struct cfs_rq *cfs_rq, struct sched_entity *se)
{
	cfs_rq->curr = se;
}

#else	/* CONFIG_FAIR_GROUP_SCHED */

static inline struct rq *rq_of(struct cfs_rq *cfs_rq)
{
	return container_of(cfs_rq, struct rq, cfs);
}

static inline struct sched_entity *cfs_rq_curr(struct cfs_rq *cfs_rq)
{
	struct rq *rq = rq_of(cfs_rq);

	if (unlikely(rq->curr->sched_class != &fair_sched_class))
		return NULL;

	return &rq->curr->se;
}

#define entity_is_task(se)	1

static inline void
set_cfs_rq_curr(struct cfs_rq *cfs_rq, struct sched_entity *se) { }

#endif	/* CONFIG_FAIR_GROUP_SCHED */

static inline struct task_struct *task_of(struct sched_entity *se)
{
	return container_of(se, struct task_struct, se);
}


/**************************************************************
 * Scheduling class tree data structure manipulation methods:
 */

/*
 * Enqueue an entity into the rb-tree:
 */
static inline void
__enqueue_entity(struct cfs_rq *cfs_rq, struct sched_entity *se)
{
	struct rb_node **link = &cfs_rq->tasks_timeline.rb_node;
	struct rb_node *parent = NULL;
	struct sched_entity *entry;
	s64 key = se->fair_key;
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
		if (key - entry->fair_key < 0) {
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
	update_load_add(&cfs_rq->load, se->load.weight);
	cfs_rq->nr_running++;
	se->on_rq = 1;
}

static inline void
__dequeue_entity(struct cfs_rq *cfs_rq, struct sched_entity *se)
{
	if (cfs_rq->rb_leftmost == &se->run_node)
		cfs_rq->rb_leftmost = rb_next(&se->run_node);
	rb_erase(&se->run_node, &cfs_rq->tasks_timeline);
	update_load_sub(&cfs_rq->load, se->load.weight);
	cfs_rq->nr_running--;
	se->on_rq = 0;
}

static inline struct rb_node *first_fair(struct cfs_rq *cfs_rq)
{
	return cfs_rq->rb_leftmost;
}

static struct sched_entity *__pick_next_entity(struct cfs_rq *cfs_rq)
{
	return rb_entry(first_fair(cfs_rq), struct sched_entity, run_node);
}

/**************************************************************
 * Scheduling class statistics methods:
 */

/*
 * We rescale the rescheduling granularity of tasks according to their
 * nice level, but only linearly, not exponentially:
 */
static long
niced_granularity(struct sched_entity *curr, unsigned long granularity)
{
	u64 tmp;

	if (likely(curr->load.weight == NICE_0_LOAD))
		return granularity;
	/*
	 * Positive nice levels get the same granularity as nice-0:
	 */
	if (likely(curr->load.weight < NICE_0_LOAD)) {
		tmp = curr->load.weight * (u64)granularity;
		return (long) (tmp >> NICE_0_SHIFT);
	}
	/*
	 * Negative nice level tasks get linearly finer
	 * granularity:
	 */
	tmp = curr->load.inv_weight * (u64)granularity;

	/*
	 * It will always fit into 'long':
	 */
	return (long) (tmp >> WMULT_SHIFT);
}

static inline void
limit_wait_runtime(struct cfs_rq *cfs_rq, struct sched_entity *se)
{
	long limit = sysctl_sched_runtime_limit;

	/*
	 * Niced tasks have the same history dynamic range as
	 * non-niced tasks:
	 */
	if (unlikely(se->wait_runtime > limit)) {
		se->wait_runtime = limit;
		schedstat_inc(se, wait_runtime_overruns);
		schedstat_inc(cfs_rq, wait_runtime_overruns);
	}
	if (unlikely(se->wait_runtime < -limit)) {
		se->wait_runtime = -limit;
		schedstat_inc(se, wait_runtime_underruns);
		schedstat_inc(cfs_rq, wait_runtime_underruns);
	}
}

static inline void
__add_wait_runtime(struct cfs_rq *cfs_rq, struct sched_entity *se, long delta)
{
	se->wait_runtime += delta;
	schedstat_add(se, sum_wait_runtime, delta);
	limit_wait_runtime(cfs_rq, se);
}

static void
add_wait_runtime(struct cfs_rq *cfs_rq, struct sched_entity *se, long delta)
{
	schedstat_add(cfs_rq, wait_runtime, -se->wait_runtime);
	__add_wait_runtime(cfs_rq, se, delta);
	schedstat_add(cfs_rq, wait_runtime, se->wait_runtime);
}

/*
 * Update the current task's runtime statistics. Skip current tasks that
 * are not in our scheduling class.
 */
static inline void
__update_curr(struct cfs_rq *cfs_rq, struct sched_entity *curr)
{
	unsigned long delta, delta_exec, delta_fair, delta_mine;
	struct load_weight *lw = &cfs_rq->load;
	unsigned long load = lw->weight;

	delta_exec = curr->delta_exec;
	schedstat_set(curr->exec_max, max((u64)delta_exec, curr->exec_max));

	curr->sum_exec_runtime += delta_exec;
	cfs_rq->exec_clock += delta_exec;

	if (unlikely(!load))
		return;

	delta_fair = calc_delta_fair(delta_exec, lw);
	delta_mine = calc_delta_mine(delta_exec, curr->load.weight, lw);

	if (cfs_rq->sleeper_bonus > sysctl_sched_granularity) {
		delta = calc_delta_mine(cfs_rq->sleeper_bonus,
					curr->load.weight, lw);
		if (unlikely(delta > cfs_rq->sleeper_bonus))
			delta = cfs_rq->sleeper_bonus;

		cfs_rq->sleeper_bonus -= delta;
		delta_mine -= delta;
	}

	cfs_rq->fair_clock += delta_fair;
	/*
	 * We executed delta_exec amount of time on the CPU,
	 * but we were only entitled to delta_mine amount of
	 * time during that period (if nr_running == 1 then
	 * the two values are equal)
	 * [Note: delta_mine - delta_exec is negative]:
	 */
	add_wait_runtime(cfs_rq, curr, delta_mine - delta_exec);
}

static void update_curr(struct cfs_rq *cfs_rq)
{
	struct sched_entity *curr = cfs_rq_curr(cfs_rq);
	unsigned long delta_exec;

	if (unlikely(!curr))
		return;

	/*
	 * Get the amount of time the current task was running
	 * since the last time we changed load (this cannot
	 * overflow on 32 bits):
	 */
	delta_exec = (unsigned long)(rq_of(cfs_rq)->clock - curr->exec_start);

	curr->delta_exec += delta_exec;

	if (unlikely(curr->delta_exec > sysctl_sched_stat_granularity)) {
		__update_curr(cfs_rq, curr);
		curr->delta_exec = 0;
	}
	curr->exec_start = rq_of(cfs_rq)->clock;
}

static inline void
update_stats_wait_start(struct cfs_rq *cfs_rq, struct sched_entity *se)
{
	se->wait_start_fair = cfs_rq->fair_clock;
	schedstat_set(se->wait_start, rq_of(cfs_rq)->clock);
}

/*
 * We calculate fair deltas here, so protect against the random effects
 * of a multiplication overflow by capping it to the runtime limit:
 */
#if BITS_PER_LONG == 32
static inline unsigned long
calc_weighted(unsigned long delta, unsigned long weight, int shift)
{
	u64 tmp = (u64)delta * weight >> shift;

	if (unlikely(tmp > sysctl_sched_runtime_limit*2))
		return sysctl_sched_runtime_limit*2;
	return tmp;
}
#else
static inline unsigned long
calc_weighted(unsigned long delta, unsigned long weight, int shift)
{
	return delta * weight >> shift;
}
#endif

/*
 * Task is being enqueued - update stats:
 */
static void update_stats_enqueue(struct cfs_rq *cfs_rq, struct sched_entity *se)
{
	s64 key;

	/*
	 * Are we enqueueing a waiting task? (for current tasks
	 * a dequeue/enqueue event is a NOP)
	 */
	if (se != cfs_rq_curr(cfs_rq))
		update_stats_wait_start(cfs_rq, se);
	/*
	 * Update the key:
	 */
	key = cfs_rq->fair_clock;

	/*
	 * Optimize the common nice 0 case:
	 */
	if (likely(se->load.weight == NICE_0_LOAD)) {
		key -= se->wait_runtime;
	} else {
		u64 tmp;

		if (se->wait_runtime < 0) {
			tmp = -se->wait_runtime;
			key += (tmp * se->load.inv_weight) >>
					(WMULT_SHIFT - NICE_0_SHIFT);
		} else {
			tmp = se->wait_runtime;
			key -= (tmp * se->load.inv_weight) >>
					(WMULT_SHIFT - NICE_0_SHIFT);
		}
	}

	se->fair_key = key;
}

/*
 * Note: must be called with a freshly updated rq->fair_clock.
 */
static inline void
__update_stats_wait_end(struct cfs_rq *cfs_rq, struct sched_entity *se)
{
	unsigned long delta_fair = se->delta_fair_run;

	schedstat_set(se->wait_max, max(se->wait_max,
			rq_of(cfs_rq)->clock - se->wait_start));

	if (unlikely(se->load.weight != NICE_0_LOAD))
		delta_fair = calc_weighted(delta_fair, se->load.weight,
							NICE_0_SHIFT);

	add_wait_runtime(cfs_rq, se, delta_fair);
}

static void
update_stats_wait_end(struct cfs_rq *cfs_rq, struct sched_entity *se)
{
	unsigned long delta_fair;

	delta_fair = (unsigned long)min((u64)(2*sysctl_sched_runtime_limit),
			(u64)(cfs_rq->fair_clock - se->wait_start_fair));

	se->delta_fair_run += delta_fair;
	if (unlikely(abs(se->delta_fair_run) >=
				sysctl_sched_stat_granularity)) {
		__update_stats_wait_end(cfs_rq, se);
		se->delta_fair_run = 0;
	}

	se->wait_start_fair = 0;
	schedstat_set(se->wait_start, 0);
}

static inline void
update_stats_dequeue(struct cfs_rq *cfs_rq, struct sched_entity *se)
{
	update_curr(cfs_rq);
	/*
	 * Mark the end of the wait period if dequeueing a
	 * waiting task:
	 */
	if (se != cfs_rq_curr(cfs_rq))
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

/*
 * We are descheduling a task - update its stats:
 */
static inline void
update_stats_curr_end(struct cfs_rq *cfs_rq, struct sched_entity *se)
{
	se->exec_start = 0;
}

/**************************************************
 * Scheduling class queueing methods:
 */

static void __enqueue_sleeper(struct cfs_rq *cfs_rq, struct sched_entity *se)
{
	unsigned long load = cfs_rq->load.weight, delta_fair;
	long prev_runtime;

	if (sysctl_sched_features & SCHED_FEAT_SLEEPER_LOAD_AVG)
		load = rq_of(cfs_rq)->cpu_load[2];

	delta_fair = se->delta_fair_sleep;

	/*
	 * Fix up delta_fair with the effect of us running
	 * during the whole sleep period:
	 */
	if (sysctl_sched_features & SCHED_FEAT_SLEEPER_AVG)
		delta_fair = div64_likely32((u64)delta_fair * load,
						load + se->load.weight);

	if (unlikely(se->load.weight != NICE_0_LOAD))
		delta_fair = calc_weighted(delta_fair, se->load.weight,
							NICE_0_SHIFT);

	prev_runtime = se->wait_runtime;
	__add_wait_runtime(cfs_rq, se, delta_fair);
	delta_fair = se->wait_runtime - prev_runtime;

	/*
	 * Track the amount of bonus we've given to sleepers:
	 */
	cfs_rq->sleeper_bonus += delta_fair;

	schedstat_add(cfs_rq, wait_runtime, se->wait_runtime);
}

static void enqueue_sleeper(struct cfs_rq *cfs_rq, struct sched_entity *se)
{
	struct task_struct *tsk = task_of(se);
	unsigned long delta_fair;

	if ((entity_is_task(se) && tsk->policy == SCHED_BATCH) ||
			 !(sysctl_sched_features & SCHED_FEAT_FAIR_SLEEPERS))
		return;

	delta_fair = (unsigned long)min((u64)(2*sysctl_sched_runtime_limit),
		(u64)(cfs_rq->fair_clock - se->sleep_start_fair));

	se->delta_fair_sleep += delta_fair;
	if (unlikely(abs(se->delta_fair_sleep) >=
				sysctl_sched_stat_granularity)) {
		__enqueue_sleeper(cfs_rq, se);
		se->delta_fair_sleep = 0;
	}

	se->sleep_start_fair = 0;

#ifdef CONFIG_SCHEDSTATS
	if (se->sleep_start) {
		u64 delta = rq_of(cfs_rq)->clock - se->sleep_start;

		if ((s64)delta < 0)
			delta = 0;

		if (unlikely(delta > se->sleep_max))
			se->sleep_max = delta;

		se->sleep_start = 0;
		se->sum_sleep_runtime += delta;
	}
	if (se->block_start) {
		u64 delta = rq_of(cfs_rq)->clock - se->block_start;

		if ((s64)delta < 0)
			delta = 0;

		if (unlikely(delta > se->block_max))
			se->block_max = delta;

		se->block_start = 0;
		se->sum_sleep_runtime += delta;
	}
#endif
}

static void
enqueue_entity(struct cfs_rq *cfs_rq, struct sched_entity *se, int wakeup)
{
	/*
	 * Update the fair clock.
	 */
	update_curr(cfs_rq);

	if (wakeup)
		enqueue_sleeper(cfs_rq, se);

	update_stats_enqueue(cfs_rq, se);
	__enqueue_entity(cfs_rq, se);
}

static void
dequeue_entity(struct cfs_rq *cfs_rq, struct sched_entity *se, int sleep)
{
	update_stats_dequeue(cfs_rq, se);
	if (sleep) {
		se->sleep_start_fair = cfs_rq->fair_clock;
#ifdef CONFIG_SCHEDSTATS
		if (entity_is_task(se)) {
			struct task_struct *tsk = task_of(se);

			if (tsk->state & TASK_INTERRUPTIBLE)
				se->sleep_start = rq_of(cfs_rq)->clock;
			if (tsk->state & TASK_UNINTERRUPTIBLE)
				se->block_start = rq_of(cfs_rq)->clock;
		}
		cfs_rq->wait_runtime -= se->wait_runtime;
#endif
	}
	__dequeue_entity(cfs_rq, se);
}

/*
 * Preempt the current task with a newly woken task if needed:
 */
static void
__check_preempt_curr_fair(struct cfs_rq *cfs_rq, struct sched_entity *se,
			  struct sched_entity *curr, unsigned long granularity)
{
	s64 __delta = curr->fair_key - se->fair_key;

	/*
	 * Take scheduling granularity into account - do not
	 * preempt the current task unless the best task has
	 * a larger than sched_granularity fairness advantage:
	 */
	if (__delta > niced_granularity(curr, granularity))
		resched_task(rq_of(cfs_rq)->curr);
}

static inline void
set_next_entity(struct cfs_rq *cfs_rq, struct sched_entity *se)
{
	/*
	 * Any task has to be enqueued before it get to execute on
	 * a CPU. So account for the time it spent waiting on the
	 * runqueue. (note, here we rely on pick_next_task() having
	 * done a put_prev_task_fair() shortly before this, which
	 * updated rq->fair_clock - used by update_stats_wait_end())
	 */
	update_stats_wait_end(cfs_rq, se);
	update_stats_curr_start(cfs_rq, se);
	set_cfs_rq_curr(cfs_rq, se);
}

static struct sched_entity *pick_next_entity(struct cfs_rq *cfs_rq)
{
	struct sched_entity *se = __pick_next_entity(cfs_rq);

	set_next_entity(cfs_rq, se);

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

	update_stats_curr_end(cfs_rq, prev);

	if (prev->on_rq)
		update_stats_wait_start(cfs_rq, prev);
	set_cfs_rq_curr(cfs_rq, NULL);
}

static void entity_tick(struct cfs_rq *cfs_rq, struct sched_entity *curr)
{
	struct sched_entity *next;

	/*
	 * Dequeue and enqueue the task to update its
	 * position within the tree:
	 */
	dequeue_entity(cfs_rq, curr, 0);
	enqueue_entity(cfs_rq, curr, 0);

	/*
	 * Reschedule if another task tops the current one.
	 */
	next = __pick_next_entity(cfs_rq);
	if (next == curr)
		return;

	__check_preempt_curr_fair(cfs_rq, next, curr, sysctl_sched_granularity);
}

/**************************************************
 * CFS operations on tasks:
 */

#ifdef CONFIG_FAIR_GROUP_SCHED

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
	/* A later patch will take group into account */
	return &cpu_rq(this_cpu)->cfs;
}

/* Iterate thr' all leaf cfs_rq's on a runqueue */
#define for_each_leaf_cfs_rq(rq, cfs_rq) \
	list_for_each_entry(cfs_rq, &rq->leaf_cfs_rq_list, leaf_cfs_rq_list)

/* Do the two (enqueued) tasks belong to the same group ? */
static inline int is_same_group(struct task_struct *curr, struct task_struct *p)
{
	if (curr->se.cfs_rq == p->se.cfs_rq)
		return 1;

	return 0;
}

#else	/* CONFIG_FAIR_GROUP_SCHED */

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

static inline int is_same_group(struct task_struct *curr, struct task_struct *p)
{
	return 1;
}

#endif	/* CONFIG_FAIR_GROUP_SCHED */

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
	}
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
	}
}

/*
 * sched_yield() support is very simple - we dequeue and enqueue
 */
static void yield_task_fair(struct rq *rq, struct task_struct *p)
{
	struct cfs_rq *cfs_rq = task_cfs_rq(p);

	__update_rq_clock(rq);
	/*
	 * Dequeue and enqueue the task to update its
	 * position within the tree:
	 */
	dequeue_entity(cfs_rq, &p->se, 0);
	enqueue_entity(cfs_rq, &p->se, 0);
}

/*
 * Preempt the current task with a newly woken task if needed:
 */
static void check_preempt_curr_fair(struct rq *rq, struct task_struct *p)
{
	struct task_struct *curr = rq->curr;
	struct cfs_rq *cfs_rq = task_cfs_rq(curr);
	unsigned long gran;

	if (unlikely(rt_prio(p->prio))) {
		update_rq_clock(rq);
		update_curr(cfs_rq);
		resched_task(curr);
		return;
	}

	gran = sysctl_sched_wakeup_granularity;
	/*
	 * Batch tasks prefer throughput over latency:
	 */
	if (unlikely(p->policy == SCHED_BATCH))
		gran = sysctl_sched_batch_wakeup_granularity;

	if (is_same_group(curr, p))
		__check_preempt_curr_fair(cfs_rq, &p->se, &curr->se, gran);
}

static struct task_struct *pick_next_task_fair(struct rq *rq)
{
	struct cfs_rq *cfs_rq = &rq->cfs;
	struct sched_entity *se;

	if (unlikely(!cfs_rq->nr_running))
		return NULL;

	do {
		se = pick_next_entity(cfs_rq);
		cfs_rq = group_cfs_rq(se);
	} while (cfs_rq);

	return task_of(se);
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
static inline struct task_struct *
__load_balance_iterator(struct cfs_rq *cfs_rq, struct rb_node *curr)
{
	struct task_struct *p;

	if (!curr)
		return NULL;

	p = rb_entry(curr, struct task_struct, se.run_node);
	cfs_rq->rb_load_balance_curr = rb_next(curr);

	return p;
}

static struct task_struct *load_balance_start_fair(void *arg)
{
	struct cfs_rq *cfs_rq = arg;

	return __load_balance_iterator(cfs_rq, first_fair(cfs_rq));
}

static struct task_struct *load_balance_next_fair(void *arg)
{
	struct cfs_rq *cfs_rq = arg;

	return __load_balance_iterator(cfs_rq, cfs_rq->rb_load_balance_curr);
}

#ifdef CONFIG_FAIR_GROUP_SCHED
static int cfs_rq_best_prio(struct cfs_rq *cfs_rq)
{
	struct sched_entity *curr;
	struct task_struct *p;

	if (!cfs_rq->nr_running)
		return MAX_PRIO;

	curr = __pick_next_entity(cfs_rq);
	p = task_of(curr);

	return p->prio;
}
#endif

static unsigned long
load_balance_fair(struct rq *this_rq, int this_cpu, struct rq *busiest,
		  unsigned long max_nr_move, unsigned long max_load_move,
		  struct sched_domain *sd, enum cpu_idle_type idle,
		  int *all_pinned, int *this_best_prio)
{
	struct cfs_rq *busy_cfs_rq;
	unsigned long load_moved, total_nr_moved = 0, nr_moved;
	long rem_load_move = max_load_move;
	struct rq_iterator cfs_rq_iterator;

	cfs_rq_iterator.start = load_balance_start_fair;
	cfs_rq_iterator.next = load_balance_next_fair;

	for_each_leaf_cfs_rq(busiest, busy_cfs_rq) {
#ifdef CONFIG_FAIR_GROUP_SCHED
		struct cfs_rq *this_cfs_rq;
		long imbalances;
		unsigned long maxload;

		this_cfs_rq = cpu_cfs_rq(busy_cfs_rq, this_cpu);

		imbalance = busy_cfs_rq->load.weight -
						 this_cfs_rq->load.weight;
		/* Don't pull if this_cfs_rq has more load than busy_cfs_rq */
		if (imbalance <= 0)
			continue;

		/* Don't pull more than imbalance/2 */
		imbalance /= 2;
		maxload = min(rem_load_move, imbalance);

		*this_best_prio = cfs_rq_best_prio(this_cfs_rq);
#else
#define maxload rem_load_move
#endif
		/* pass busy_cfs_rq argument into
		 * load_balance_[start|next]_fair iterators
		 */
		cfs_rq_iterator.arg = busy_cfs_rq;
		nr_moved = balance_tasks(this_rq, this_cpu, busiest,
				max_nr_move, maxload, sd, idle, all_pinned,
				&load_moved, this_best_prio, &cfs_rq_iterator);

		total_nr_moved += nr_moved;
		max_nr_move -= nr_moved;
		rem_load_move -= load_moved;

		if (max_nr_move <= 0 || rem_load_move <= 0)
			break;
	}

	return max_load_move - rem_load_move;
}

/*
 * scheduler tick hitting a task of our scheduling class:
 */
static void task_tick_fair(struct rq *rq, struct task_struct *curr)
{
	struct cfs_rq *cfs_rq;
	struct sched_entity *se = &curr->se;

	for_each_sched_entity(se) {
		cfs_rq = cfs_rq_of(se);
		entity_tick(cfs_rq, se);
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
	struct sched_entity *se = &p->se;

	sched_info_queued(p);

	update_stats_enqueue(cfs_rq, se);
	/*
	 * Child runs first: we let it run before the parent
	 * until it reschedules once. We set up the key so that
	 * it will preempt the parent:
	 */
	p->se.fair_key = current->se.fair_key -
		niced_granularity(&rq->curr->se, sysctl_sched_granularity) - 1;
	/*
	 * The first wait is dominated by the child-runs-first logic,
	 * so do not credit it with that waiting time yet:
	 */
	if (sysctl_sched_features & SCHED_FEAT_SKIP_INITIAL)
		p->se.wait_start_fair = 0;

	/*
	 * The statistical average of wait_runtime is about
	 * -granularity/2, so initialize the task with that:
	 */
	if (sysctl_sched_features & SCHED_FEAT_START_DEBIT)
		p->se.wait_runtime = -(sysctl_sched_granularity / 2);

	__enqueue_entity(cfs_rq, se);
}

#ifdef CONFIG_FAIR_GROUP_SCHED
/* Account for a task changing its policy or group.
 *
 * This routine is mostly called to set cfs_rq->curr field when a task
 * migrates between groups/classes.
 */
static void set_curr_task_fair(struct rq *rq)
{
	struct sched_entity *se = &rq->curr.se;

	for_each_sched_entity(se)
		set_next_entity(cfs_rq_of(se), se);
}
#else
static void set_curr_task_fair(struct rq *rq)
{
}
#endif

/*
 * All the scheduling class methods:
 */
struct sched_class fair_sched_class __read_mostly = {
	.enqueue_task		= enqueue_task_fair,
	.dequeue_task		= dequeue_task_fair,
	.yield_task		= yield_task_fair,

	.check_preempt_curr	= check_preempt_curr_fair,

	.pick_next_task		= pick_next_task_fair,
	.put_prev_task		= put_prev_task_fair,

	.load_balance		= load_balance_fair,

	.set_curr_task          = set_curr_task_fair,
	.task_tick		= task_tick_fair,
	.task_new		= task_new_fair,
};

#ifdef CONFIG_SCHED_DEBUG
static void print_cfs_stats(struct seq_file *m, int cpu)
{
	struct cfs_rq *cfs_rq;

	for_each_leaf_cfs_rq(cpu_rq(cpu), cfs_rq)
		print_cfs_rq(m, cpu, cfs_rq);
}
#endif
