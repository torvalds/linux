/*
 * idle-task scheduling class.
 *
 * (NOTE: these are not related to SCHED_IDLE tasks which are
 *  handled in sched_fair.c)
 */

/*
 * Idle tasks are unconditionally rescheduled:
 */
static void check_preempt_curr_idle(struct rq *rq, struct task_struct *p)
{
	resched_task(rq->idle);
}

static struct task_struct *pick_next_task_idle(struct rq *rq, u64 now)
{
	schedstat_inc(rq, sched_goidle);

	return rq->idle;
}

/*
 * It is not legal to sleep in the idle task - print a warning
 * message if some code attempts to do it:
 */
static void
dequeue_task_idle(struct rq *rq, struct task_struct *p, int sleep, u64 now)
{
	spin_unlock_irq(&rq->lock);
	printk(KERN_ERR "bad: scheduling from the idle thread!\n");
	dump_stack();
	spin_lock_irq(&rq->lock);
}

static void put_prev_task_idle(struct rq *rq, struct task_struct *prev, u64 now)
{
}

static int
load_balance_idle(struct rq *this_rq, int this_cpu, struct rq *busiest,
			unsigned long max_nr_move, unsigned long max_load_move,
			struct sched_domain *sd, enum cpu_idle_type idle,
			int *all_pinned, unsigned long *total_load_moved)
{
	return 0;
}

static void task_tick_idle(struct rq *rq, struct task_struct *curr)
{
}

/*
 * Simple, special scheduling class for the per-CPU idle tasks:
 */
static struct sched_class idle_sched_class __read_mostly = {
	/* no enqueue/yield_task for idle tasks */

	/* dequeue is not valid, we print a debug message there: */
	.dequeue_task		= dequeue_task_idle,

	.check_preempt_curr	= check_preempt_curr_idle,

	.pick_next_task		= pick_next_task_idle,
	.put_prev_task		= put_prev_task_idle,

	.load_balance		= load_balance_idle,

	.task_tick		= task_tick_idle,
	/* no .task_new for idle tasks */
};
