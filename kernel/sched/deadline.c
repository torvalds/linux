/*
 * Deadline Scheduling Class (SCHED_DEADLINE)
 *
 * Earliest Deadline First (EDF) + Constant Bandwidth Server (CBS).
 *
 * Tasks that periodically executes their instances for less than their
 * runtime won't miss any of their deadlines.
 * Tasks that are not periodic or sporadic or that tries to execute more
 * than their reserved bandwidth will be slowed down (and may potentially
 * miss some of their deadlines), and won't affect any other task.
 *
 * Copyright (C) 2012 Dario Faggioli <raistlin@linux.it>,
 *                    Juri Lelli <juri.lelli@gmail.com>,
 *                    Michael Trimarchi <michael@amarulasolutions.com>,
 *                    Fabio Checconi <fchecconi@gmail.com>
 */
#include "sched.h"

#include <linux/slab.h>

struct dl_bandwidth def_dl_bandwidth;

static inline struct task_struct *dl_task_of(struct sched_dl_entity *dl_se)
{
	return container_of(dl_se, struct task_struct, dl);
}

static inline struct rq *rq_of_dl_rq(struct dl_rq *dl_rq)
{
	return container_of(dl_rq, struct rq, dl);
}

static inline struct dl_rq *dl_rq_of_se(struct sched_dl_entity *dl_se)
{
	struct task_struct *p = dl_task_of(dl_se);
	struct rq *rq = task_rq(p);

	return &rq->dl;
}

static inline int on_dl_rq(struct sched_dl_entity *dl_se)
{
	return !RB_EMPTY_NODE(&dl_se->rb_node);
}

static inline int is_leftmost(struct task_struct *p, struct dl_rq *dl_rq)
{
	struct sched_dl_entity *dl_se = &p->dl;

	return dl_rq->rb_leftmost == &dl_se->rb_node;
}

void init_dl_bandwidth(struct dl_bandwidth *dl_b, u64 period, u64 runtime)
{
	raw_spin_lock_init(&dl_b->dl_runtime_lock);
	dl_b->dl_period = period;
	dl_b->dl_runtime = runtime;
}

extern unsigned long to_ratio(u64 period, u64 runtime);

void init_dl_bw(struct dl_bw *dl_b)
{
	raw_spin_lock_init(&dl_b->lock);
	raw_spin_lock(&def_dl_bandwidth.dl_runtime_lock);
	if (global_rt_runtime() == RUNTIME_INF)
		dl_b->bw = -1;
	else
		dl_b->bw = to_ratio(global_rt_period(), global_rt_runtime());
	raw_spin_unlock(&def_dl_bandwidth.dl_runtime_lock);
	dl_b->total_bw = 0;
}

void init_dl_rq(struct dl_rq *dl_rq, struct rq *rq)
{
	dl_rq->rb_root = RB_ROOT;

#ifdef CONFIG_SMP
	/* zero means no -deadline tasks */
	dl_rq->earliest_dl.curr = dl_rq->earliest_dl.next = 0;

	dl_rq->dl_nr_migratory = 0;
	dl_rq->overloaded = 0;
	dl_rq->pushable_dl_tasks_root = RB_ROOT;
#else
	init_dl_bw(&dl_rq->dl_bw);
#endif
}

#ifdef CONFIG_SMP

static inline int dl_overloaded(struct rq *rq)
{
	return atomic_read(&rq->rd->dlo_count);
}

static inline void dl_set_overload(struct rq *rq)
{
	if (!rq->online)
		return;

	cpumask_set_cpu(rq->cpu, rq->rd->dlo_mask);
	/*
	 * Must be visible before the overload count is
	 * set (as in sched_rt.c).
	 *
	 * Matched by the barrier in pull_dl_task().
	 */
	smp_wmb();
	atomic_inc(&rq->rd->dlo_count);
}

static inline void dl_clear_overload(struct rq *rq)
{
	if (!rq->online)
		return;

	atomic_dec(&rq->rd->dlo_count);
	cpumask_clear_cpu(rq->cpu, rq->rd->dlo_mask);
}

static void update_dl_migration(struct dl_rq *dl_rq)
{
	if (dl_rq->dl_nr_migratory && dl_rq->dl_nr_running > 1) {
		if (!dl_rq->overloaded) {
			dl_set_overload(rq_of_dl_rq(dl_rq));
			dl_rq->overloaded = 1;
		}
	} else if (dl_rq->overloaded) {
		dl_clear_overload(rq_of_dl_rq(dl_rq));
		dl_rq->overloaded = 0;
	}
}

static void inc_dl_migration(struct sched_dl_entity *dl_se, struct dl_rq *dl_rq)
{
	struct task_struct *p = dl_task_of(dl_se);

	if (p->nr_cpus_allowed > 1)
		dl_rq->dl_nr_migratory++;

	update_dl_migration(dl_rq);
}

static void dec_dl_migration(struct sched_dl_entity *dl_se, struct dl_rq *dl_rq)
{
	struct task_struct *p = dl_task_of(dl_se);

	if (p->nr_cpus_allowed > 1)
		dl_rq->dl_nr_migratory--;

	update_dl_migration(dl_rq);
}

/*
 * The list of pushable -deadline task is not a plist, like in
 * sched_rt.c, it is an rb-tree with tasks ordered by deadline.
 */
static void enqueue_pushable_dl_task(struct rq *rq, struct task_struct *p)
{
	struct dl_rq *dl_rq = &rq->dl;
	struct rb_node **link = &dl_rq->pushable_dl_tasks_root.rb_node;
	struct rb_node *parent = NULL;
	struct task_struct *entry;
	int leftmost = 1;

	BUG_ON(!RB_EMPTY_NODE(&p->pushable_dl_tasks));

	while (*link) {
		parent = *link;
		entry = rb_entry(parent, struct task_struct,
				 pushable_dl_tasks);
		if (dl_entity_preempt(&p->dl, &entry->dl))
			link = &parent->rb_left;
		else {
			link = &parent->rb_right;
			leftmost = 0;
		}
	}

	if (leftmost)
		dl_rq->pushable_dl_tasks_leftmost = &p->pushable_dl_tasks;

	rb_link_node(&p->pushable_dl_tasks, parent, link);
	rb_insert_color(&p->pushable_dl_tasks, &dl_rq->pushable_dl_tasks_root);
}

static void dequeue_pushable_dl_task(struct rq *rq, struct task_struct *p)
{
	struct dl_rq *dl_rq = &rq->dl;

	if (RB_EMPTY_NODE(&p->pushable_dl_tasks))
		return;

	if (dl_rq->pushable_dl_tasks_leftmost == &p->pushable_dl_tasks) {
		struct rb_node *next_node;

		next_node = rb_next(&p->pushable_dl_tasks);
		dl_rq->pushable_dl_tasks_leftmost = next_node;
	}

	rb_erase(&p->pushable_dl_tasks, &dl_rq->pushable_dl_tasks_root);
	RB_CLEAR_NODE(&p->pushable_dl_tasks);
}

static inline int has_pushable_dl_tasks(struct rq *rq)
{
	return !RB_EMPTY_ROOT(&rq->dl.pushable_dl_tasks_root);
}

static int push_dl_task(struct rq *rq);

#else

static inline
void enqueue_pushable_dl_task(struct rq *rq, struct task_struct *p)
{
}

static inline
void dequeue_pushable_dl_task(struct rq *rq, struct task_struct *p)
{
}

static inline
void inc_dl_migration(struct sched_dl_entity *dl_se, struct dl_rq *dl_rq)
{
}

static inline
void dec_dl_migration(struct sched_dl_entity *dl_se, struct dl_rq *dl_rq)
{
}

#endif /* CONFIG_SMP */

static void enqueue_task_dl(struct rq *rq, struct task_struct *p, int flags);
static void __dequeue_task_dl(struct rq *rq, struct task_struct *p, int flags);
static void check_preempt_curr_dl(struct rq *rq, struct task_struct *p,
				  int flags);

/*
 * We are being explicitly informed that a new instance is starting,
 * and this means that:
 *  - the absolute deadline of the entity has to be placed at
 *    current time + relative deadline;
 *  - the runtime of the entity has to be set to the maximum value.
 *
 * The capability of specifying such event is useful whenever a -deadline
 * entity wants to (try to!) synchronize its behaviour with the scheduler's
 * one, and to (try to!) reconcile itself with its own scheduling
 * parameters.
 */
static inline void setup_new_dl_entity(struct sched_dl_entity *dl_se,
				       struct sched_dl_entity *pi_se)
{
	struct dl_rq *dl_rq = dl_rq_of_se(dl_se);
	struct rq *rq = rq_of_dl_rq(dl_rq);

	WARN_ON(!dl_se->dl_new || dl_se->dl_throttled);

	/*
	 * We use the regular wall clock time to set deadlines in the
	 * future; in fact, we must consider execution overheads (time
	 * spent on hardirq context, etc.).
	 */
	dl_se->deadline = rq_clock(rq) + pi_se->dl_deadline;
	dl_se->runtime = pi_se->dl_runtime;
	dl_se->dl_new = 0;
}

/*
 * Pure Earliest Deadline First (EDF) scheduling does not deal with the
 * possibility of a entity lasting more than what it declared, and thus
 * exhausting its runtime.
 *
 * Here we are interested in making runtime overrun possible, but we do
 * not want a entity which is misbehaving to affect the scheduling of all
 * other entities.
 * Therefore, a budgeting strategy called Constant Bandwidth Server (CBS)
 * is used, in order to confine each entity within its own bandwidth.
 *
 * This function deals exactly with that, and ensures that when the runtime
 * of a entity is replenished, its deadline is also postponed. That ensures
 * the overrunning entity can't interfere with other entity in the system and
 * can't make them miss their deadlines. Reasons why this kind of overruns
 * could happen are, typically, a entity voluntarily trying to overcome its
 * runtime, or it just underestimated it during sched_setscheduler_ex().
 */
static void replenish_dl_entity(struct sched_dl_entity *dl_se,
				struct sched_dl_entity *pi_se)
{
	struct dl_rq *dl_rq = dl_rq_of_se(dl_se);
	struct rq *rq = rq_of_dl_rq(dl_rq);

	BUG_ON(pi_se->dl_runtime <= 0);

	/*
	 * This could be the case for a !-dl task that is boosted.
	 * Just go with full inherited parameters.
	 */
	if (dl_se->dl_deadline == 0) {
		dl_se->deadline = rq_clock(rq) + pi_se->dl_deadline;
		dl_se->runtime = pi_se->dl_runtime;
	}

	/*
	 * We keep moving the deadline away until we get some
	 * available runtime for the entity. This ensures correct
	 * handling of situations where the runtime overrun is
	 * arbitrary large.
	 */
	while (dl_se->runtime <= 0) {
		dl_se->deadline += pi_se->dl_period;
		dl_se->runtime += pi_se->dl_runtime;
	}

	/*
	 * At this point, the deadline really should be "in
	 * the future" with respect to rq->clock. If it's
	 * not, we are, for some reason, lagging too much!
	 * Anyway, after having warn userspace abut that,
	 * we still try to keep the things running by
	 * resetting the deadline and the budget of the
	 * entity.
	 */
	if (dl_time_before(dl_se->deadline, rq_clock(rq))) {
		static bool lag_once = false;

		if (!lag_once) {
			lag_once = true;
			printk_sched("sched: DL replenish lagged to much\n");
		}
		dl_se->deadline = rq_clock(rq) + pi_se->dl_deadline;
		dl_se->runtime = pi_se->dl_runtime;
	}
}

/*
 * Here we check if --at time t-- an entity (which is probably being
 * [re]activated or, in general, enqueued) can use its remaining runtime
 * and its current deadline _without_ exceeding the bandwidth it is
 * assigned (function returns true if it can't). We are in fact applying
 * one of the CBS rules: when a task wakes up, if the residual runtime
 * over residual deadline fits within the allocated bandwidth, then we
 * can keep the current (absolute) deadline and residual budget without
 * disrupting the schedulability of the system. Otherwise, we should
 * refill the runtime and set the deadline a period in the future,
 * because keeping the current (absolute) deadline of the task would
 * result in breaking guarantees promised to other tasks (refer to
 * Documentation/scheduler/sched-deadline.txt for more informations).
 *
 * This function returns true if:
 *
 *   runtime / (deadline - t) > dl_runtime / dl_period ,
 *
 * IOW we can't recycle current parameters.
 *
 * Notice that the bandwidth check is done against the period. For
 * task with deadline equal to period this is the same of using
 * dl_deadline instead of dl_period in the equation above.
 */
static bool dl_entity_overflow(struct sched_dl_entity *dl_se,
			       struct sched_dl_entity *pi_se, u64 t)
{
	u64 left, right;

	/*
	 * left and right are the two sides of the equation above,
	 * after a bit of shuffling to use multiplications instead
	 * of divisions.
	 *
	 * Note that none of the time values involved in the two
	 * multiplications are absolute: dl_deadline and dl_runtime
	 * are the relative deadline and the maximum runtime of each
	 * instance, runtime is the runtime left for the last instance
	 * and (deadline - t), since t is rq->clock, is the time left
	 * to the (absolute) deadline. Even if overflowing the u64 type
	 * is very unlikely to occur in both cases, here we scale down
	 * as we want to avoid that risk at all. Scaling down by 10
	 * means that we reduce granularity to 1us. We are fine with it,
	 * since this is only a true/false check and, anyway, thinking
	 * of anything below microseconds resolution is actually fiction
	 * (but still we want to give the user that illusion >;).
	 */
	left = (pi_se->dl_period >> DL_SCALE) * (dl_se->runtime >> DL_SCALE);
	right = ((dl_se->deadline - t) >> DL_SCALE) *
		(pi_se->dl_runtime >> DL_SCALE);

	return dl_time_before(right, left);
}

/*
 * When a -deadline entity is queued back on the runqueue, its runtime and
 * deadline might need updating.
 *
 * The policy here is that we update the deadline of the entity only if:
 *  - the current deadline is in the past,
 *  - using the remaining runtime with the current deadline would make
 *    the entity exceed its bandwidth.
 */
static void update_dl_entity(struct sched_dl_entity *dl_se,
			     struct sched_dl_entity *pi_se)
{
	struct dl_rq *dl_rq = dl_rq_of_se(dl_se);
	struct rq *rq = rq_of_dl_rq(dl_rq);

	/*
	 * The arrival of a new instance needs special treatment, i.e.,
	 * the actual scheduling parameters have to be "renewed".
	 */
	if (dl_se->dl_new) {
		setup_new_dl_entity(dl_se, pi_se);
		return;
	}

	if (dl_time_before(dl_se->deadline, rq_clock(rq)) ||
	    dl_entity_overflow(dl_se, pi_se, rq_clock(rq))) {
		dl_se->deadline = rq_clock(rq) + pi_se->dl_deadline;
		dl_se->runtime = pi_se->dl_runtime;
	}
}

/*
 * If the entity depleted all its runtime, and if we want it to sleep
 * while waiting for some new execution time to become available, we
 * set the bandwidth enforcement timer to the replenishment instant
 * and try to activate it.
 *
 * Notice that it is important for the caller to know if the timer
 * actually started or not (i.e., the replenishment instant is in
 * the future or in the past).
 */
static int start_dl_timer(struct sched_dl_entity *dl_se, bool boosted)
{
	struct dl_rq *dl_rq = dl_rq_of_se(dl_se);
	struct rq *rq = rq_of_dl_rq(dl_rq);
	ktime_t now, act;
	ktime_t soft, hard;
	unsigned long range;
	s64 delta;

	if (boosted)
		return 0;
	/*
	 * We want the timer to fire at the deadline, but considering
	 * that it is actually coming from rq->clock and not from
	 * hrtimer's time base reading.
	 */
	act = ns_to_ktime(dl_se->deadline);
	now = hrtimer_cb_get_time(&dl_se->dl_timer);
	delta = ktime_to_ns(now) - rq_clock(rq);
	act = ktime_add_ns(act, delta);

	/*
	 * If the expiry time already passed, e.g., because the value
	 * chosen as the deadline is too small, don't even try to
	 * start the timer in the past!
	 */
	if (ktime_us_delta(act, now) < 0)
		return 0;

	hrtimer_set_expires(&dl_se->dl_timer, act);

	soft = hrtimer_get_softexpires(&dl_se->dl_timer);
	hard = hrtimer_get_expires(&dl_se->dl_timer);
	range = ktime_to_ns(ktime_sub(hard, soft));
	__hrtimer_start_range_ns(&dl_se->dl_timer, soft,
				 range, HRTIMER_MODE_ABS, 0);

	return hrtimer_active(&dl_se->dl_timer);
}

/*
 * This is the bandwidth enforcement timer callback. If here, we know
 * a task is not on its dl_rq, since the fact that the timer was running
 * means the task is throttled and needs a runtime replenishment.
 *
 * However, what we actually do depends on the fact the task is active,
 * (it is on its rq) or has been removed from there by a call to
 * dequeue_task_dl(). In the former case we must issue the runtime
 * replenishment and add the task back to the dl_rq; in the latter, we just
 * do nothing but clearing dl_throttled, so that runtime and deadline
 * updating (and the queueing back to dl_rq) will be done by the
 * next call to enqueue_task_dl().
 */
static enum hrtimer_restart dl_task_timer(struct hrtimer *timer)
{
	struct sched_dl_entity *dl_se = container_of(timer,
						     struct sched_dl_entity,
						     dl_timer);
	struct task_struct *p = dl_task_of(dl_se);
	struct rq *rq = task_rq(p);
	raw_spin_lock(&rq->lock);

	/*
	 * We need to take care of a possible races here. In fact, the
	 * task might have changed its scheduling policy to something
	 * different from SCHED_DEADLINE or changed its reservation
	 * parameters (through sched_setscheduler()).
	 */
	if (!dl_task(p) || dl_se->dl_new)
		goto unlock;

	sched_clock_tick();
	update_rq_clock(rq);
	dl_se->dl_throttled = 0;
	if (p->on_rq) {
		enqueue_task_dl(rq, p, ENQUEUE_REPLENISH);
		if (task_has_dl_policy(rq->curr))
			check_preempt_curr_dl(rq, p, 0);
		else
			resched_task(rq->curr);
#ifdef CONFIG_SMP
		/*
		 * Queueing this task back might have overloaded rq,
		 * check if we need to kick someone away.
		 */
		if (has_pushable_dl_tasks(rq))
			push_dl_task(rq);
#endif
	}
unlock:
	raw_spin_unlock(&rq->lock);

	return HRTIMER_NORESTART;
}

void init_dl_task_timer(struct sched_dl_entity *dl_se)
{
	struct hrtimer *timer = &dl_se->dl_timer;

	if (hrtimer_active(timer)) {
		hrtimer_try_to_cancel(timer);
		return;
	}

	hrtimer_init(timer, CLOCK_MONOTONIC, HRTIMER_MODE_REL);
	timer->function = dl_task_timer;
}

static
int dl_runtime_exceeded(struct rq *rq, struct sched_dl_entity *dl_se)
{
	int dmiss = dl_time_before(dl_se->deadline, rq_clock(rq));
	int rorun = dl_se->runtime <= 0;

	if (!rorun && !dmiss)
		return 0;

	/*
	 * If we are beyond our current deadline and we are still
	 * executing, then we have already used some of the runtime of
	 * the next instance. Thus, if we do not account that, we are
	 * stealing bandwidth from the system at each deadline miss!
	 */
	if (dmiss) {
		dl_se->runtime = rorun ? dl_se->runtime : 0;
		dl_se->runtime -= rq_clock(rq) - dl_se->deadline;
	}

	return 1;
}

extern bool sched_rt_bandwidth_account(struct rt_rq *rt_rq);

/*
 * Update the current task's runtime statistics (provided it is still
 * a -deadline task and has not been removed from the dl_rq).
 */
static void update_curr_dl(struct rq *rq)
{
	struct task_struct *curr = rq->curr;
	struct sched_dl_entity *dl_se = &curr->dl;
	u64 delta_exec;

	if (!dl_task(curr) || !on_dl_rq(dl_se))
		return;

	/*
	 * Consumed budget is computed considering the time as
	 * observed by schedulable tasks (excluding time spent
	 * in hardirq context, etc.). Deadlines are instead
	 * computed using hard walltime. This seems to be the more
	 * natural solution, but the full ramifications of this
	 * approach need further study.
	 */
	delta_exec = rq_clock_task(rq) - curr->se.exec_start;
	if (unlikely((s64)delta_exec < 0))
		delta_exec = 0;

	schedstat_set(curr->se.statistics.exec_max,
		      max(curr->se.statistics.exec_max, delta_exec));

	curr->se.sum_exec_runtime += delta_exec;
	account_group_exec_runtime(curr, delta_exec);

	curr->se.exec_start = rq_clock_task(rq);
	cpuacct_charge(curr, delta_exec);

	sched_rt_avg_update(rq, delta_exec);

	dl_se->runtime -= delta_exec;
	if (dl_runtime_exceeded(rq, dl_se)) {
		__dequeue_task_dl(rq, curr, 0);
		if (likely(start_dl_timer(dl_se, curr->dl.dl_boosted)))
			dl_se->dl_throttled = 1;
		else
			enqueue_task_dl(rq, curr, ENQUEUE_REPLENISH);

		if (!is_leftmost(curr, &rq->dl))
			resched_task(curr);
	}

	/*
	 * Because -- for now -- we share the rt bandwidth, we need to
	 * account our runtime there too, otherwise actual rt tasks
	 * would be able to exceed the shared quota.
	 *
	 * Account to the root rt group for now.
	 *
	 * The solution we're working towards is having the RT groups scheduled
	 * using deadline servers -- however there's a few nasties to figure
	 * out before that can happen.
	 */
	if (rt_bandwidth_enabled()) {
		struct rt_rq *rt_rq = &rq->rt;

		raw_spin_lock(&rt_rq->rt_runtime_lock);
		/*
		 * We'll let actual RT tasks worry about the overflow here, we
		 * have our own CBS to keep us inline; only account when RT
		 * bandwidth is relevant.
		 */
		if (sched_rt_bandwidth_account(rt_rq))
			rt_rq->rt_time += delta_exec;
		raw_spin_unlock(&rt_rq->rt_runtime_lock);
	}
}

#ifdef CONFIG_SMP

static struct task_struct *pick_next_earliest_dl_task(struct rq *rq, int cpu);

static inline u64 next_deadline(struct rq *rq)
{
	struct task_struct *next = pick_next_earliest_dl_task(rq, rq->cpu);

	if (next && dl_prio(next->prio))
		return next->dl.deadline;
	else
		return 0;
}

static void inc_dl_deadline(struct dl_rq *dl_rq, u64 deadline)
{
	struct rq *rq = rq_of_dl_rq(dl_rq);

	if (dl_rq->earliest_dl.curr == 0 ||
	    dl_time_before(deadline, dl_rq->earliest_dl.curr)) {
		/*
		 * If the dl_rq had no -deadline tasks, or if the new task
		 * has shorter deadline than the current one on dl_rq, we
		 * know that the previous earliest becomes our next earliest,
		 * as the new task becomes the earliest itself.
		 */
		dl_rq->earliest_dl.next = dl_rq->earliest_dl.curr;
		dl_rq->earliest_dl.curr = deadline;
		cpudl_set(&rq->rd->cpudl, rq->cpu, deadline, 1);
	} else if (dl_rq->earliest_dl.next == 0 ||
		   dl_time_before(deadline, dl_rq->earliest_dl.next)) {
		/*
		 * On the other hand, if the new -deadline task has a
		 * a later deadline than the earliest one on dl_rq, but
		 * it is earlier than the next (if any), we must
		 * recompute the next-earliest.
		 */
		dl_rq->earliest_dl.next = next_deadline(rq);
	}
}

static void dec_dl_deadline(struct dl_rq *dl_rq, u64 deadline)
{
	struct rq *rq = rq_of_dl_rq(dl_rq);

	/*
	 * Since we may have removed our earliest (and/or next earliest)
	 * task we must recompute them.
	 */
	if (!dl_rq->dl_nr_running) {
		dl_rq->earliest_dl.curr = 0;
		dl_rq->earliest_dl.next = 0;
		cpudl_set(&rq->rd->cpudl, rq->cpu, 0, 0);
	} else {
		struct rb_node *leftmost = dl_rq->rb_leftmost;
		struct sched_dl_entity *entry;

		entry = rb_entry(leftmost, struct sched_dl_entity, rb_node);
		dl_rq->earliest_dl.curr = entry->deadline;
		dl_rq->earliest_dl.next = next_deadline(rq);
		cpudl_set(&rq->rd->cpudl, rq->cpu, entry->deadline, 1);
	}
}

#else

static inline void inc_dl_deadline(struct dl_rq *dl_rq, u64 deadline) {}
static inline void dec_dl_deadline(struct dl_rq *dl_rq, u64 deadline) {}

#endif /* CONFIG_SMP */

static inline
void inc_dl_tasks(struct sched_dl_entity *dl_se, struct dl_rq *dl_rq)
{
	int prio = dl_task_of(dl_se)->prio;
	u64 deadline = dl_se->deadline;

	WARN_ON(!dl_prio(prio));
	dl_rq->dl_nr_running++;
	inc_nr_running(rq_of_dl_rq(dl_rq));

	inc_dl_deadline(dl_rq, deadline);
	inc_dl_migration(dl_se, dl_rq);
}

static inline
void dec_dl_tasks(struct sched_dl_entity *dl_se, struct dl_rq *dl_rq)
{
	int prio = dl_task_of(dl_se)->prio;

	WARN_ON(!dl_prio(prio));
	WARN_ON(!dl_rq->dl_nr_running);
	dl_rq->dl_nr_running--;
	dec_nr_running(rq_of_dl_rq(dl_rq));

	dec_dl_deadline(dl_rq, dl_se->deadline);
	dec_dl_migration(dl_se, dl_rq);
}

static void __enqueue_dl_entity(struct sched_dl_entity *dl_se)
{
	struct dl_rq *dl_rq = dl_rq_of_se(dl_se);
	struct rb_node **link = &dl_rq->rb_root.rb_node;
	struct rb_node *parent = NULL;
	struct sched_dl_entity *entry;
	int leftmost = 1;

	BUG_ON(!RB_EMPTY_NODE(&dl_se->rb_node));

	while (*link) {
		parent = *link;
		entry = rb_entry(parent, struct sched_dl_entity, rb_node);
		if (dl_time_before(dl_se->deadline, entry->deadline))
			link = &parent->rb_left;
		else {
			link = &parent->rb_right;
			leftmost = 0;
		}
	}

	if (leftmost)
		dl_rq->rb_leftmost = &dl_se->rb_node;

	rb_link_node(&dl_se->rb_node, parent, link);
	rb_insert_color(&dl_se->rb_node, &dl_rq->rb_root);

	inc_dl_tasks(dl_se, dl_rq);
}

static void __dequeue_dl_entity(struct sched_dl_entity *dl_se)
{
	struct dl_rq *dl_rq = dl_rq_of_se(dl_se);

	if (RB_EMPTY_NODE(&dl_se->rb_node))
		return;

	if (dl_rq->rb_leftmost == &dl_se->rb_node) {
		struct rb_node *next_node;

		next_node = rb_next(&dl_se->rb_node);
		dl_rq->rb_leftmost = next_node;
	}

	rb_erase(&dl_se->rb_node, &dl_rq->rb_root);
	RB_CLEAR_NODE(&dl_se->rb_node);

	dec_dl_tasks(dl_se, dl_rq);
}

static void
enqueue_dl_entity(struct sched_dl_entity *dl_se,
		  struct sched_dl_entity *pi_se, int flags)
{
	BUG_ON(on_dl_rq(dl_se));

	/*
	 * If this is a wakeup or a new instance, the scheduling
	 * parameters of the task might need updating. Otherwise,
	 * we want a replenishment of its runtime.
	 */
	if (!dl_se->dl_new && flags & ENQUEUE_REPLENISH)
		replenish_dl_entity(dl_se, pi_se);
	else
		update_dl_entity(dl_se, pi_se);

	__enqueue_dl_entity(dl_se);
}

static void dequeue_dl_entity(struct sched_dl_entity *dl_se)
{
	__dequeue_dl_entity(dl_se);
}

static void enqueue_task_dl(struct rq *rq, struct task_struct *p, int flags)
{
	struct task_struct *pi_task = rt_mutex_get_top_task(p);
	struct sched_dl_entity *pi_se = &p->dl;

	/*
	 * Use the scheduling parameters of the top pi-waiter
	 * task if we have one and its (relative) deadline is
	 * smaller than our one... OTW we keep our runtime and
	 * deadline.
	 */
	if (pi_task && p->dl.dl_boosted && dl_prio(pi_task->normal_prio))
		pi_se = &pi_task->dl;

	/*
	 * If p is throttled, we do nothing. In fact, if it exhausted
	 * its budget it needs a replenishment and, since it now is on
	 * its rq, the bandwidth timer callback (which clearly has not
	 * run yet) will take care of this.
	 */
	if (p->dl.dl_throttled)
		return;

	enqueue_dl_entity(&p->dl, pi_se, flags);

	if (!task_current(rq, p) && p->nr_cpus_allowed > 1)
		enqueue_pushable_dl_task(rq, p);
}

static void __dequeue_task_dl(struct rq *rq, struct task_struct *p, int flags)
{
	dequeue_dl_entity(&p->dl);
	dequeue_pushable_dl_task(rq, p);
}

static void dequeue_task_dl(struct rq *rq, struct task_struct *p, int flags)
{
	update_curr_dl(rq);
	__dequeue_task_dl(rq, p, flags);
}

/*
 * Yield task semantic for -deadline tasks is:
 *
 *   get off from the CPU until our next instance, with
 *   a new runtime. This is of little use now, since we
 *   don't have a bandwidth reclaiming mechanism. Anyway,
 *   bandwidth reclaiming is planned for the future, and
 *   yield_task_dl will indicate that some spare budget
 *   is available for other task instances to use it.
 */
static void yield_task_dl(struct rq *rq)
{
	struct task_struct *p = rq->curr;

	/*
	 * We make the task go to sleep until its current deadline by
	 * forcing its runtime to zero. This way, update_curr_dl() stops
	 * it and the bandwidth timer will wake it up and will give it
	 * new scheduling parameters (thanks to dl_new=1).
	 */
	if (p->dl.runtime > 0) {
		rq->curr->dl.dl_new = 1;
		p->dl.runtime = 0;
	}
	update_curr_dl(rq);
}

#ifdef CONFIG_SMP

static int find_later_rq(struct task_struct *task);

static int
select_task_rq_dl(struct task_struct *p, int cpu, int sd_flag, int flags)
{
	struct task_struct *curr;
	struct rq *rq;

	if (sd_flag != SD_BALANCE_WAKE && sd_flag != SD_BALANCE_FORK)
		goto out;

	rq = cpu_rq(cpu);

	rcu_read_lock();
	curr = ACCESS_ONCE(rq->curr); /* unlocked access */

	/*
	 * If we are dealing with a -deadline task, we must
	 * decide where to wake it up.
	 * If it has a later deadline and the current task
	 * on this rq can't move (provided the waking task
	 * can!) we prefer to send it somewhere else. On the
	 * other hand, if it has a shorter deadline, we
	 * try to make it stay here, it might be important.
	 */
	if (unlikely(dl_task(curr)) &&
	    (curr->nr_cpus_allowed < 2 ||
	     !dl_entity_preempt(&p->dl, &curr->dl)) &&
	    (p->nr_cpus_allowed > 1)) {
		int target = find_later_rq(p);

		if (target != -1)
			cpu = target;
	}
	rcu_read_unlock();

out:
	return cpu;
}

static void check_preempt_equal_dl(struct rq *rq, struct task_struct *p)
{
	/*
	 * Current can't be migrated, useless to reschedule,
	 * let's hope p can move out.
	 */
	if (rq->curr->nr_cpus_allowed == 1 ||
	    cpudl_find(&rq->rd->cpudl, rq->curr, NULL) == -1)
		return;

	/*
	 * p is migratable, so let's not schedule it and
	 * see if it is pushed or pulled somewhere else.
	 */
	if (p->nr_cpus_allowed != 1 &&
	    cpudl_find(&rq->rd->cpudl, p, NULL) != -1)
		return;

	resched_task(rq->curr);
}

#endif /* CONFIG_SMP */

/*
 * Only called when both the current and waking task are -deadline
 * tasks.
 */
static void check_preempt_curr_dl(struct rq *rq, struct task_struct *p,
				  int flags)
{
	if (dl_entity_preempt(&p->dl, &rq->curr->dl)) {
		resched_task(rq->curr);
		return;
	}

#ifdef CONFIG_SMP
	/*
	 * In the unlikely case current and p have the same deadline
	 * let us try to decide what's the best thing to do...
	 */
	if ((p->dl.deadline == rq->curr->dl.deadline) &&
	    !test_tsk_need_resched(rq->curr))
		check_preempt_equal_dl(rq, p);
#endif /* CONFIG_SMP */
}

#ifdef CONFIG_SCHED_HRTICK
static void start_hrtick_dl(struct rq *rq, struct task_struct *p)
{
	s64 delta = p->dl.dl_runtime - p->dl.runtime;

	if (delta > 10000)
		hrtick_start(rq, p->dl.runtime);
}
#endif

static struct sched_dl_entity *pick_next_dl_entity(struct rq *rq,
						   struct dl_rq *dl_rq)
{
	struct rb_node *left = dl_rq->rb_leftmost;

	if (!left)
		return NULL;

	return rb_entry(left, struct sched_dl_entity, rb_node);
}

struct task_struct *pick_next_task_dl(struct rq *rq)
{
	struct sched_dl_entity *dl_se;
	struct task_struct *p;
	struct dl_rq *dl_rq;

	dl_rq = &rq->dl;

	if (unlikely(!dl_rq->dl_nr_running))
		return NULL;

	dl_se = pick_next_dl_entity(rq, dl_rq);
	BUG_ON(!dl_se);

	p = dl_task_of(dl_se);
	p->se.exec_start = rq_clock_task(rq);

	/* Running task will never be pushed. */
       dequeue_pushable_dl_task(rq, p);

#ifdef CONFIG_SCHED_HRTICK
	if (hrtick_enabled(rq))
		start_hrtick_dl(rq, p);
#endif

#ifdef CONFIG_SMP
	rq->post_schedule = has_pushable_dl_tasks(rq);
#endif /* CONFIG_SMP */

	return p;
}

static void put_prev_task_dl(struct rq *rq, struct task_struct *p)
{
	update_curr_dl(rq);

	if (on_dl_rq(&p->dl) && p->nr_cpus_allowed > 1)
		enqueue_pushable_dl_task(rq, p);
}

static void task_tick_dl(struct rq *rq, struct task_struct *p, int queued)
{
	update_curr_dl(rq);

#ifdef CONFIG_SCHED_HRTICK
	if (hrtick_enabled(rq) && queued && p->dl.runtime > 0)
		start_hrtick_dl(rq, p);
#endif
}

static void task_fork_dl(struct task_struct *p)
{
	/*
	 * SCHED_DEADLINE tasks cannot fork and this is achieved through
	 * sched_fork()
	 */
}

static void task_dead_dl(struct task_struct *p)
{
	struct hrtimer *timer = &p->dl.dl_timer;
	struct dl_bw *dl_b = dl_bw_of(task_cpu(p));

	/*
	 * Since we are TASK_DEAD we won't slip out of the domain!
	 */
	raw_spin_lock_irq(&dl_b->lock);
	dl_b->total_bw -= p->dl.dl_bw;
	raw_spin_unlock_irq(&dl_b->lock);

	hrtimer_cancel(timer);
}

static void set_curr_task_dl(struct rq *rq)
{
	struct task_struct *p = rq->curr;

	p->se.exec_start = rq_clock_task(rq);

	/* You can't push away the running task */
	dequeue_pushable_dl_task(rq, p);
}

#ifdef CONFIG_SMP

/* Only try algorithms three times */
#define DL_MAX_TRIES 3

static int pick_dl_task(struct rq *rq, struct task_struct *p, int cpu)
{
	if (!task_running(rq, p) &&
	    (cpu < 0 || cpumask_test_cpu(cpu, &p->cpus_allowed)) &&
	    (p->nr_cpus_allowed > 1))
		return 1;

	return 0;
}

/* Returns the second earliest -deadline task, NULL otherwise */
static struct task_struct *pick_next_earliest_dl_task(struct rq *rq, int cpu)
{
	struct rb_node *next_node = rq->dl.rb_leftmost;
	struct sched_dl_entity *dl_se;
	struct task_struct *p = NULL;

next_node:
	next_node = rb_next(next_node);
	if (next_node) {
		dl_se = rb_entry(next_node, struct sched_dl_entity, rb_node);
		p = dl_task_of(dl_se);

		if (pick_dl_task(rq, p, cpu))
			return p;

		goto next_node;
	}

	return NULL;
}

static DEFINE_PER_CPU(cpumask_var_t, local_cpu_mask_dl);

static int find_later_rq(struct task_struct *task)
{
	struct sched_domain *sd;
	struct cpumask *later_mask = __get_cpu_var(local_cpu_mask_dl);
	int this_cpu = smp_processor_id();
	int best_cpu, cpu = task_cpu(task);

	/* Make sure the mask is initialized first */
	if (unlikely(!later_mask))
		return -1;

	if (task->nr_cpus_allowed == 1)
		return -1;

	best_cpu = cpudl_find(&task_rq(task)->rd->cpudl,
			task, later_mask);
	if (best_cpu == -1)
		return -1;

	/*
	 * If we are here, some target has been found,
	 * the most suitable of which is cached in best_cpu.
	 * This is, among the runqueues where the current tasks
	 * have later deadlines than the task's one, the rq
	 * with the latest possible one.
	 *
	 * Now we check how well this matches with task's
	 * affinity and system topology.
	 *
	 * The last cpu where the task run is our first
	 * guess, since it is most likely cache-hot there.
	 */
	if (cpumask_test_cpu(cpu, later_mask))
		return cpu;
	/*
	 * Check if this_cpu is to be skipped (i.e., it is
	 * not in the mask) or not.
	 */
	if (!cpumask_test_cpu(this_cpu, later_mask))
		this_cpu = -1;

	rcu_read_lock();
	for_each_domain(cpu, sd) {
		if (sd->flags & SD_WAKE_AFFINE) {

			/*
			 * If possible, preempting this_cpu is
			 * cheaper than migrating.
			 */
			if (this_cpu != -1 &&
			    cpumask_test_cpu(this_cpu, sched_domain_span(sd))) {
				rcu_read_unlock();
				return this_cpu;
			}

			/*
			 * Last chance: if best_cpu is valid and is
			 * in the mask, that becomes our choice.
			 */
			if (best_cpu < nr_cpu_ids &&
			    cpumask_test_cpu(best_cpu, sched_domain_span(sd))) {
				rcu_read_unlock();
				return best_cpu;
			}
		}
	}
	rcu_read_unlock();

	/*
	 * At this point, all our guesses failed, we just return
	 * 'something', and let the caller sort the things out.
	 */
	if (this_cpu != -1)
		return this_cpu;

	cpu = cpumask_any(later_mask);
	if (cpu < nr_cpu_ids)
		return cpu;

	return -1;
}

/* Locks the rq it finds */
static struct rq *find_lock_later_rq(struct task_struct *task, struct rq *rq)
{
	struct rq *later_rq = NULL;
	int tries;
	int cpu;

	for (tries = 0; tries < DL_MAX_TRIES; tries++) {
		cpu = find_later_rq(task);

		if ((cpu == -1) || (cpu == rq->cpu))
			break;

		later_rq = cpu_rq(cpu);

		/* Retry if something changed. */
		if (double_lock_balance(rq, later_rq)) {
			if (unlikely(task_rq(task) != rq ||
				     !cpumask_test_cpu(later_rq->cpu,
				                       &task->cpus_allowed) ||
				     task_running(rq, task) || !task->on_rq)) {
				double_unlock_balance(rq, later_rq);
				later_rq = NULL;
				break;
			}
		}

		/*
		 * If the rq we found has no -deadline task, or
		 * its earliest one has a later deadline than our
		 * task, the rq is a good one.
		 */
		if (!later_rq->dl.dl_nr_running ||
		    dl_time_before(task->dl.deadline,
				   later_rq->dl.earliest_dl.curr))
			break;

		/* Otherwise we try again. */
		double_unlock_balance(rq, later_rq);
		later_rq = NULL;
	}

	return later_rq;
}

static struct task_struct *pick_next_pushable_dl_task(struct rq *rq)
{
	struct task_struct *p;

	if (!has_pushable_dl_tasks(rq))
		return NULL;

	p = rb_entry(rq->dl.pushable_dl_tasks_leftmost,
		     struct task_struct, pushable_dl_tasks);

	BUG_ON(rq->cpu != task_cpu(p));
	BUG_ON(task_current(rq, p));
	BUG_ON(p->nr_cpus_allowed <= 1);

	BUG_ON(!p->on_rq);
	BUG_ON(!dl_task(p));

	return p;
}

/*
 * See if the non running -deadline tasks on this rq
 * can be sent to some other CPU where they can preempt
 * and start executing.
 */
static int push_dl_task(struct rq *rq)
{
	struct task_struct *next_task;
	struct rq *later_rq;

	if (!rq->dl.overloaded)
		return 0;

	next_task = pick_next_pushable_dl_task(rq);
	if (!next_task)
		return 0;

retry:
	if (unlikely(next_task == rq->curr)) {
		WARN_ON(1);
		return 0;
	}

	/*
	 * If next_task preempts rq->curr, and rq->curr
	 * can move away, it makes sense to just reschedule
	 * without going further in pushing next_task.
	 */
	if (dl_task(rq->curr) &&
	    dl_time_before(next_task->dl.deadline, rq->curr->dl.deadline) &&
	    rq->curr->nr_cpus_allowed > 1) {
		resched_task(rq->curr);
		return 0;
	}

	/* We might release rq lock */
	get_task_struct(next_task);

	/* Will lock the rq it'll find */
	later_rq = find_lock_later_rq(next_task, rq);
	if (!later_rq) {
		struct task_struct *task;

		/*
		 * We must check all this again, since
		 * find_lock_later_rq releases rq->lock and it is
		 * then possible that next_task has migrated.
		 */
		task = pick_next_pushable_dl_task(rq);
		if (task_cpu(next_task) == rq->cpu && task == next_task) {
			/*
			 * The task is still there. We don't try
			 * again, some other cpu will pull it when ready.
			 */
			dequeue_pushable_dl_task(rq, next_task);
			goto out;
		}

		if (!task)
			/* No more tasks */
			goto out;

		put_task_struct(next_task);
		next_task = task;
		goto retry;
	}

	deactivate_task(rq, next_task, 0);
	set_task_cpu(next_task, later_rq->cpu);
	activate_task(later_rq, next_task, 0);

	resched_task(later_rq->curr);

	double_unlock_balance(rq, later_rq);

out:
	put_task_struct(next_task);

	return 1;
}

static void push_dl_tasks(struct rq *rq)
{
	/* Terminates as it moves a -deadline task */
	while (push_dl_task(rq))
		;
}

static int pull_dl_task(struct rq *this_rq)
{
	int this_cpu = this_rq->cpu, ret = 0, cpu;
	struct task_struct *p;
	struct rq *src_rq;
	u64 dmin = LONG_MAX;

	if (likely(!dl_overloaded(this_rq)))
		return 0;

	/*
	 * Match the barrier from dl_set_overloaded; this guarantees that if we
	 * see overloaded we must also see the dlo_mask bit.
	 */
	smp_rmb();

	for_each_cpu(cpu, this_rq->rd->dlo_mask) {
		if (this_cpu == cpu)
			continue;

		src_rq = cpu_rq(cpu);

		/*
		 * It looks racy, abd it is! However, as in sched_rt.c,
		 * we are fine with this.
		 */
		if (this_rq->dl.dl_nr_running &&
		    dl_time_before(this_rq->dl.earliest_dl.curr,
				   src_rq->dl.earliest_dl.next))
			continue;

		/* Might drop this_rq->lock */
		double_lock_balance(this_rq, src_rq);

		/*
		 * If there are no more pullable tasks on the
		 * rq, we're done with it.
		 */
		if (src_rq->dl.dl_nr_running <= 1)
			goto skip;

		p = pick_next_earliest_dl_task(src_rq, this_cpu);

		/*
		 * We found a task to be pulled if:
		 *  - it preempts our current (if there's one),
		 *  - it will preempt the last one we pulled (if any).
		 */
		if (p && dl_time_before(p->dl.deadline, dmin) &&
		    (!this_rq->dl.dl_nr_running ||
		     dl_time_before(p->dl.deadline,
				    this_rq->dl.earliest_dl.curr))) {
			WARN_ON(p == src_rq->curr);
			WARN_ON(!p->on_rq);

			/*
			 * Then we pull iff p has actually an earlier
			 * deadline than the current task of its runqueue.
			 */
			if (dl_time_before(p->dl.deadline,
					   src_rq->curr->dl.deadline))
				goto skip;

			ret = 1;

			deactivate_task(src_rq, p, 0);
			set_task_cpu(p, this_cpu);
			activate_task(this_rq, p, 0);
			dmin = p->dl.deadline;

			/* Is there any other task even earlier? */
		}
skip:
		double_unlock_balance(this_rq, src_rq);
	}

	return ret;
}

static void pre_schedule_dl(struct rq *rq, struct task_struct *prev)
{
	/* Try to pull other tasks here */
	if (dl_task(prev))
		pull_dl_task(rq);
}

static void post_schedule_dl(struct rq *rq)
{
	push_dl_tasks(rq);
}

/*
 * Since the task is not running and a reschedule is not going to happen
 * anytime soon on its runqueue, we try pushing it away now.
 */
static void task_woken_dl(struct rq *rq, struct task_struct *p)
{
	if (!task_running(rq, p) &&
	    !test_tsk_need_resched(rq->curr) &&
	    has_pushable_dl_tasks(rq) &&
	    p->nr_cpus_allowed > 1 &&
	    dl_task(rq->curr) &&
	    (rq->curr->nr_cpus_allowed < 2 ||
	     dl_entity_preempt(&rq->curr->dl, &p->dl))) {
		push_dl_tasks(rq);
	}
}

static void set_cpus_allowed_dl(struct task_struct *p,
				const struct cpumask *new_mask)
{
	struct rq *rq;
	int weight;

	BUG_ON(!dl_task(p));

	/*
	 * Update only if the task is actually running (i.e.,
	 * it is on the rq AND it is not throttled).
	 */
	if (!on_dl_rq(&p->dl))
		return;

	weight = cpumask_weight(new_mask);

	/*
	 * Only update if the process changes its state from whether it
	 * can migrate or not.
	 */
	if ((p->nr_cpus_allowed > 1) == (weight > 1))
		return;

	rq = task_rq(p);

	/*
	 * The process used to be able to migrate OR it can now migrate
	 */
	if (weight <= 1) {
		if (!task_current(rq, p))
			dequeue_pushable_dl_task(rq, p);
		BUG_ON(!rq->dl.dl_nr_migratory);
		rq->dl.dl_nr_migratory--;
	} else {
		if (!task_current(rq, p))
			enqueue_pushable_dl_task(rq, p);
		rq->dl.dl_nr_migratory++;
	}

	update_dl_migration(&rq->dl);
}

/* Assumes rq->lock is held */
static void rq_online_dl(struct rq *rq)
{
	if (rq->dl.overloaded)
		dl_set_overload(rq);

	if (rq->dl.dl_nr_running > 0)
		cpudl_set(&rq->rd->cpudl, rq->cpu, rq->dl.earliest_dl.curr, 1);
}

/* Assumes rq->lock is held */
static void rq_offline_dl(struct rq *rq)
{
	if (rq->dl.overloaded)
		dl_clear_overload(rq);

	cpudl_set(&rq->rd->cpudl, rq->cpu, 0, 0);
}

void init_sched_dl_class(void)
{
	unsigned int i;

	for_each_possible_cpu(i)
		zalloc_cpumask_var_node(&per_cpu(local_cpu_mask_dl, i),
					GFP_KERNEL, cpu_to_node(i));
}

#endif /* CONFIG_SMP */

static void switched_from_dl(struct rq *rq, struct task_struct *p)
{
	if (hrtimer_active(&p->dl.dl_timer) && !dl_policy(p->policy))
		hrtimer_try_to_cancel(&p->dl.dl_timer);

#ifdef CONFIG_SMP
	/*
	 * Since this might be the only -deadline task on the rq,
	 * this is the right place to try to pull some other one
	 * from an overloaded cpu, if any.
	 */
	if (!rq->dl.dl_nr_running)
		pull_dl_task(rq);
#endif
}

/*
 * When switching to -deadline, we may overload the rq, then
 * we try to push someone off, if possible.
 */
static void switched_to_dl(struct rq *rq, struct task_struct *p)
{
	int check_resched = 1;

	/*
	 * If p is throttled, don't consider the possibility
	 * of preempting rq->curr, the check will be done right
	 * after its runtime will get replenished.
	 */
	if (unlikely(p->dl.dl_throttled))
		return;

	if (p->on_rq || rq->curr != p) {
#ifdef CONFIG_SMP
		if (rq->dl.overloaded && push_dl_task(rq) && rq != task_rq(p))
			/* Only reschedule if pushing failed */
			check_resched = 0;
#endif /* CONFIG_SMP */
		if (check_resched && task_has_dl_policy(rq->curr))
			check_preempt_curr_dl(rq, p, 0);
	}
}

/*
 * If the scheduling parameters of a -deadline task changed,
 * a push or pull operation might be needed.
 */
static void prio_changed_dl(struct rq *rq, struct task_struct *p,
			    int oldprio)
{
	if (p->on_rq || rq->curr == p) {
#ifdef CONFIG_SMP
		/*
		 * This might be too much, but unfortunately
		 * we don't have the old deadline value, and
		 * we can't argue if the task is increasing
		 * or lowering its prio, so...
		 */
		if (!rq->dl.overloaded)
			pull_dl_task(rq);

		/*
		 * If we now have a earlier deadline task than p,
		 * then reschedule, provided p is still on this
		 * runqueue.
		 */
		if (dl_time_before(rq->dl.earliest_dl.curr, p->dl.deadline) &&
		    rq->curr == p)
			resched_task(p);
#else
		/*
		 * Again, we don't know if p has a earlier
		 * or later deadline, so let's blindly set a
		 * (maybe not needed) rescheduling point.
		 */
		resched_task(p);
#endif /* CONFIG_SMP */
	} else
		switched_to_dl(rq, p);
}

const struct sched_class dl_sched_class = {
	.next			= &rt_sched_class,
	.enqueue_task		= enqueue_task_dl,
	.dequeue_task		= dequeue_task_dl,
	.yield_task		= yield_task_dl,

	.check_preempt_curr	= check_preempt_curr_dl,

	.pick_next_task		= pick_next_task_dl,
	.put_prev_task		= put_prev_task_dl,

#ifdef CONFIG_SMP
	.select_task_rq		= select_task_rq_dl,
	.set_cpus_allowed       = set_cpus_allowed_dl,
	.rq_online              = rq_online_dl,
	.rq_offline             = rq_offline_dl,
	.pre_schedule		= pre_schedule_dl,
	.post_schedule		= post_schedule_dl,
	.task_woken		= task_woken_dl,
#endif

	.set_curr_task		= set_curr_task_dl,
	.task_tick		= task_tick_dl,
	.task_fork              = task_fork_dl,
	.task_dead		= task_dead_dl,

	.prio_changed           = prio_changed_dl,
	.switched_from		= switched_from_dl,
	.switched_to		= switched_to_dl,
};
