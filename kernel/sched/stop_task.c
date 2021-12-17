// SPDX-License-Identifier: GPL-2.0
/*
 * stop-task scheduling class.
 *
 * The stop task is the highest priority task in the system, it preempts
 * everything and will be preempted by nothing.
 *
 * See kernel/stop_machine.c
 */
#include "sched.h"

#ifdef CONFIG_SMP
static int
select_task_rq_stop(struct task_struct *p, int cpu, int flags)
{
	return task_cpu(p); /* stop tasks as never migrate */
}

static int
balance_stop(struct rq *rq, struct task_struct *prev, struct rq_flags *rf)
{
	return sched_stop_runnable(rq);
}
#endif /* CONFIG_SMP */

static void
check_preempt_curr_stop(struct rq *rq, struct task_struct *p, int flags)
{
	/* we're never preempted */
}

static void set_next_task_stop(struct rq *rq, struct task_struct *stop, bool first)
{
	stop->se.exec_start = rq_clock_task(rq);
}

static struct task_struct *pick_task_stop(struct rq *rq)
{
	if (!sched_stop_runnable(rq))
		return NULL;

	return rq->stop;
}

static struct task_struct *pick_next_task_stop(struct rq *rq)
{
	struct task_struct *p = pick_task_stop(rq);

	if (p)
		set_next_task_stop(rq, p, true);

	return p;
}

static void
enqueue_task_stop(struct rq *rq, struct task_struct *p, int flags)
{
	add_nr_running(rq, 1);
}

static void
dequeue_task_stop(struct rq *rq, struct task_struct *p, int flags)
{
	sub_nr_running(rq, 1);
}

static void yield_task_stop(struct rq *rq)
{
	BUG(); /* the stop task should never yield, its pointless. */
}

static void put_prev_task_stop(struct rq *rq, struct task_struct *prev)
{
	struct task_struct *curr = rq->curr;
	u64 delta_exec;

	delta_exec = rq_clock_task(rq) - curr->se.exec_start;
	if (unlikely((s64)delta_exec < 0))
		delta_exec = 0;

	schedstat_set(curr->stats.exec_max,
		      max(curr->stats.exec_max, delta_exec));

	curr->se.sum_exec_runtime += delta_exec;
	account_group_exec_runtime(curr, delta_exec);

	curr->se.exec_start = rq_clock_task(rq);
	cgroup_account_cputime(curr, delta_exec);
}

/*
 * scheduler tick hitting a task of our scheduling class.
 *
 * NOTE: This function can be called remotely by the tick offload that
 * goes along full dynticks. Therefore no local assumption can be made
 * and everything must be accessed through the @rq and @curr passed in
 * parameters.
 */
static void task_tick_stop(struct rq *rq, struct task_struct *curr, int queued)
{
}

static void switched_to_stop(struct rq *rq, struct task_struct *p)
{
	BUG(); /* its impossible to change to this class */
}

static void
prio_changed_stop(struct rq *rq, struct task_struct *p, int oldprio)
{
	BUG(); /* how!?, what priority? */
}

static void update_curr_stop(struct rq *rq)
{
}

/*
 * Simple, special scheduling class for the per-CPU stop tasks:
 */
DEFINE_SCHED_CLASS(stop) = {

	.enqueue_task		= enqueue_task_stop,
	.dequeue_task		= dequeue_task_stop,
	.yield_task		= yield_task_stop,

	.check_preempt_curr	= check_preempt_curr_stop,

	.pick_next_task		= pick_next_task_stop,
	.put_prev_task		= put_prev_task_stop,
	.set_next_task          = set_next_task_stop,

#ifdef CONFIG_SMP
	.balance		= balance_stop,
	.pick_task		= pick_task_stop,
	.select_task_rq		= select_task_rq_stop,
	.set_cpus_allowed	= set_cpus_allowed_common,
#endif

	.task_tick		= task_tick_stop,

	.prio_changed		= prio_changed_stop,
	.switched_to		= switched_to_stop,
	.update_curr		= update_curr_stop,
};
