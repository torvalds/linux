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
 *                    Michael Trimarchi <michael@amarulasolutions.com>,
 *                    Fabio Checconi <fchecconi@gmail.com>
 */
#include "sched.h"

static inline int dl_time_before(u64 a, u64 b)
{
	return (s64)(a - b) < 0;
}

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

void init_dl_rq(struct dl_rq *dl_rq, struct rq *rq)
{
	dl_rq->rb_root = RB_ROOT;
}

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
static inline void setup_new_dl_entity(struct sched_dl_entity *dl_se)
{
	struct dl_rq *dl_rq = dl_rq_of_se(dl_se);
	struct rq *rq = rq_of_dl_rq(dl_rq);

	WARN_ON(!dl_se->dl_new || dl_se->dl_throttled);

	/*
	 * We use the regular wall clock time to set deadlines in the
	 * future; in fact, we must consider execution overheads (time
	 * spent on hardirq context, etc.).
	 */
	dl_se->deadline = rq_clock(rq) + dl_se->dl_deadline;
	dl_se->runtime = dl_se->dl_runtime;
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
static void replenish_dl_entity(struct sched_dl_entity *dl_se)
{
	struct dl_rq *dl_rq = dl_rq_of_se(dl_se);
	struct rq *rq = rq_of_dl_rq(dl_rq);

	/*
	 * We keep moving the deadline away until we get some
	 * available runtime for the entity. This ensures correct
	 * handling of situations where the runtime overrun is
	 * arbitrary large.
	 */
	while (dl_se->runtime <= 0) {
		dl_se->deadline += dl_se->dl_deadline;
		dl_se->runtime += dl_se->dl_runtime;
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
		dl_se->deadline = rq_clock(rq) + dl_se->dl_deadline;
		dl_se->runtime = dl_se->dl_runtime;
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
 * result in breaking guarantees promised to other tasks.
 *
 * This function returns true if:
 *
 *   runtime / (deadline - t) > dl_runtime / dl_deadline ,
 *
 * IOW we can't recycle current parameters.
 */
static bool dl_entity_overflow(struct sched_dl_entity *dl_se, u64 t)
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
	left = (dl_se->dl_deadline >> 10) * (dl_se->runtime >> 10);
	right = ((dl_se->deadline - t) >> 10) * (dl_se->dl_runtime >> 10);

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
static void update_dl_entity(struct sched_dl_entity *dl_se)
{
	struct dl_rq *dl_rq = dl_rq_of_se(dl_se);
	struct rq *rq = rq_of_dl_rq(dl_rq);

	/*
	 * The arrival of a new instance needs special treatment, i.e.,
	 * the actual scheduling parameters have to be "renewed".
	 */
	if (dl_se->dl_new) {
		setup_new_dl_entity(dl_se);
		return;
	}

	if (dl_time_before(dl_se->deadline, rq_clock(rq)) ||
	    dl_entity_overflow(dl_se, rq_clock(rq))) {
		dl_se->deadline = rq_clock(rq) + dl_se->dl_deadline;
		dl_se->runtime = dl_se->dl_runtime;
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
static int start_dl_timer(struct sched_dl_entity *dl_se)
{
	struct dl_rq *dl_rq = dl_rq_of_se(dl_se);
	struct rq *rq = rq_of_dl_rq(dl_rq);
	ktime_t now, act;
	ktime_t soft, hard;
	unsigned long range;
	s64 delta;

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

	dl_se->runtime -= delta_exec;
	if (dl_runtime_exceeded(rq, dl_se)) {
		__dequeue_task_dl(rq, curr, 0);
		if (likely(start_dl_timer(dl_se)))
			dl_se->dl_throttled = 1;
		else
			enqueue_task_dl(rq, curr, ENQUEUE_REPLENISH);

		if (!is_leftmost(curr, &rq->dl))
			resched_task(curr);
	}
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

	dl_rq->dl_nr_running++;
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

	dl_rq->dl_nr_running--;
}

static void
enqueue_dl_entity(struct sched_dl_entity *dl_se, int flags)
{
	BUG_ON(on_dl_rq(dl_se));

	/*
	 * If this is a wakeup or a new instance, the scheduling
	 * parameters of the task might need updating. Otherwise,
	 * we want a replenishment of its runtime.
	 */
	if (!dl_se->dl_new && flags & ENQUEUE_REPLENISH)
		replenish_dl_entity(dl_se);
	else
		update_dl_entity(dl_se);

	__enqueue_dl_entity(dl_se);
}

static void dequeue_dl_entity(struct sched_dl_entity *dl_se)
{
	__dequeue_dl_entity(dl_se);
}

static void enqueue_task_dl(struct rq *rq, struct task_struct *p, int flags)
{
	/*
	 * If p is throttled, we do nothing. In fact, if it exhausted
	 * its budget it needs a replenishment and, since it now is on
	 * its rq, the bandwidth timer callback (which clearly has not
	 * run yet) will take care of this.
	 */
	if (p->dl.dl_throttled)
		return;

	enqueue_dl_entity(&p->dl, flags);
	inc_nr_running(rq);
}

static void __dequeue_task_dl(struct rq *rq, struct task_struct *p, int flags)
{
	dequeue_dl_entity(&p->dl);
}

static void dequeue_task_dl(struct rq *rq, struct task_struct *p, int flags)
{
	update_curr_dl(rq);
	__dequeue_task_dl(rq, p, flags);

	dec_nr_running(rq);
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

/*
 * Only called when both the current and waking task are -deadline
 * tasks.
 */
static void check_preempt_curr_dl(struct rq *rq, struct task_struct *p,
				  int flags)
{
	if (dl_time_before(p->dl.deadline, rq->curr->dl.deadline))
		resched_task(rq->curr);
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
#ifdef CONFIG_SCHED_HRTICK
	if (hrtick_enabled(rq))
		start_hrtick_dl(rq, p);
#endif
	return p;
}

static void put_prev_task_dl(struct rq *rq, struct task_struct *p)
{
	update_curr_dl(rq);
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

	if (hrtimer_active(timer))
		hrtimer_try_to_cancel(timer);
}

static void set_curr_task_dl(struct rq *rq)
{
	struct task_struct *p = rq->curr;

	p->se.exec_start = rq_clock_task(rq);
}

static void switched_from_dl(struct rq *rq, struct task_struct *p)
{
	if (hrtimer_active(&p->dl.dl_timer))
		hrtimer_try_to_cancel(&p->dl.dl_timer);
}

static void switched_to_dl(struct rq *rq, struct task_struct *p)
{
	/*
	 * If p is throttled, don't consider the possibility
	 * of preempting rq->curr, the check will be done right
	 * after its runtime will get replenished.
	 */
	if (unlikely(p->dl.dl_throttled))
		return;

	if (p->on_rq || rq->curr != p) {
		if (task_has_dl_policy(rq->curr))
			check_preempt_curr_dl(rq, p, 0);
		else
			resched_task(rq->curr);
	}
}

static void prio_changed_dl(struct rq *rq, struct task_struct *p,
			    int oldprio)
{
	switched_to_dl(rq, p);
}

#ifdef CONFIG_SMP
static int
select_task_rq_dl(struct task_struct *p, int prev_cpu, int sd_flag, int flags)
{
	return task_cpu(p);
}
#endif

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
#endif

	.set_curr_task		= set_curr_task_dl,
	.task_tick		= task_tick_dl,
	.task_fork              = task_fork_dl,
	.task_dead		= task_dead_dl,

	.prio_changed           = prio_changed_dl,
	.switched_from		= switched_from_dl,
	.switched_to		= switched_to_dl,
};
