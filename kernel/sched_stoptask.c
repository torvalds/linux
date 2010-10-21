/*
 * stop-task scheduling class.
 *
 * The stop task is the highest priority task in the system, it preempts
 * everything and will be preempted by nothing.
 *
 * See kernel/stop_machine.c
 */

#ifdef CONFIG_SMP
static int
select_task_rq_stop(struct rq *rq, struct task_struct *p,
		    int sd_flag, int flags)
{
	return task_cpu(p); /* stop tasks as never migrate */
}
#endif /* CONFIG_SMP */

static void
check_preempt_curr_stop(struct rq *rq, struct task_struct *p, int flags)
{
	resched_task(rq->curr); /* we preempt everything */
}

static struct task_struct *pick_next_task_stop(struct rq *rq)
{
	struct task_struct *stop = rq->stop;

	if (stop && stop->state == TASK_RUNNING)
		return stop;

	return NULL;
}

static void
enqueue_task_stop(struct rq *rq, struct task_struct *p, int flags)
{
}

static void
dequeue_task_stop(struct rq *rq, struct task_struct *p, int flags)
{
}

static void yield_task_stop(struct rq *rq)
{
	BUG(); /* the stop task should never yield, its pointless. */
}

static void put_prev_task_stop(struct rq *rq, struct task_struct *prev)
{
}

static void task_tick_stop(struct rq *rq, struct task_struct *curr, int queued)
{
}

static void set_curr_task_stop(struct rq *rq)
{
}

static void switched_to_stop(struct rq *rq, struct task_struct *p,
			     int running)
{
	BUG(); /* its impossible to change to this class */
}

static void prio_changed_stop(struct rq *rq, struct task_struct *p,
			      int oldprio, int running)
{
	BUG(); /* how!?, what priority? */
}

static unsigned int
get_rr_interval_stop(struct rq *rq, struct task_struct *task)
{
	return 0;
}

/*
 * Simple, special scheduling class for the per-CPU stop tasks:
 */
static const struct sched_class stop_sched_class = {
	.next			= &rt_sched_class,

	.enqueue_task		= enqueue_task_stop,
	.dequeue_task		= dequeue_task_stop,
	.yield_task		= yield_task_stop,

	.check_preempt_curr	= check_preempt_curr_stop,

	.pick_next_task		= pick_next_task_stop,
	.put_prev_task		= put_prev_task_stop,

#ifdef CONFIG_SMP
	.select_task_rq		= select_task_rq_stop,
#endif

	.set_curr_task          = set_curr_task_stop,
	.task_tick		= task_tick_stop,

	.get_rr_interval	= get_rr_interval_stop,

	.prio_changed		= prio_changed_stop,
	.switched_to		= switched_to_stop,

	/* no .task_new for stop tasks */
};
