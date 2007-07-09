/*
 * Real-Time Scheduling Class (mapped to the SCHED_FIFO and SCHED_RR
 * policies)
 */

/*
 * Update the current task's runtime statistics. Skip current tasks that
 * are not in our scheduling class.
 */
static inline void update_curr_rt(struct rq *rq, u64 now)
{
	struct task_struct *curr = rq->curr;
	u64 delta_exec;

	if (!task_has_rt_policy(curr))
		return;

	delta_exec = now - curr->se.exec_start;
	if (unlikely((s64)delta_exec < 0))
		delta_exec = 0;
	if (unlikely(delta_exec > curr->se.exec_max))
		curr->se.exec_max = delta_exec;

	curr->se.sum_exec_runtime += delta_exec;
	curr->se.exec_start = now;
}

static void
enqueue_task_rt(struct rq *rq, struct task_struct *p, int wakeup, u64 now)
{
	struct rt_prio_array *array = &rq->rt.active;

	list_add_tail(&p->run_list, array->queue + p->prio);
	__set_bit(p->prio, array->bitmap);
}

/*
 * Adding/removing a task to/from a priority array:
 */
static void
dequeue_task_rt(struct rq *rq, struct task_struct *p, int sleep, u64 now)
{
	struct rt_prio_array *array = &rq->rt.active;

	update_curr_rt(rq, now);

	list_del(&p->run_list);
	if (list_empty(array->queue + p->prio))
		__clear_bit(p->prio, array->bitmap);
}

/*
 * Put task to the end of the run list without the overhead of dequeue
 * followed by enqueue.
 */
static void requeue_task_rt(struct rq *rq, struct task_struct *p)
{
	struct rt_prio_array *array = &rq->rt.active;

	list_move_tail(&p->run_list, array->queue + p->prio);
}

static void
yield_task_rt(struct rq *rq, struct task_struct *p)
{
	requeue_task_rt(rq, p);
}

/*
 * Preempt the current task with a newly woken task if needed:
 */
static void check_preempt_curr_rt(struct rq *rq, struct task_struct *p)
{
	if (p->prio < rq->curr->prio)
		resched_task(rq->curr);
}

static struct task_struct *pick_next_task_rt(struct rq *rq, u64 now)
{
	struct rt_prio_array *array = &rq->rt.active;
	struct task_struct *next;
	struct list_head *queue;
	int idx;

	idx = sched_find_first_bit(array->bitmap);
	if (idx >= MAX_RT_PRIO)
		return NULL;

	queue = array->queue + idx;
	next = list_entry(queue->next, struct task_struct, run_list);

	next->se.exec_start = now;

	return next;
}

static void put_prev_task_rt(struct rq *rq, struct task_struct *p, u64 now)
{
	update_curr_rt(rq, now);
	p->se.exec_start = 0;
}

/*
 * Load-balancing iterator. Note: while the runqueue stays locked
 * during the whole iteration, the current task might be
 * dequeued so the iterator has to be dequeue-safe. Here we
 * achieve that by always pre-iterating before returning
 * the current task:
 */
static struct task_struct *load_balance_start_rt(void *arg)
{
	struct rq *rq = arg;
	struct rt_prio_array *array = &rq->rt.active;
	struct list_head *head, *curr;
	struct task_struct *p;
	int idx;

	idx = sched_find_first_bit(array->bitmap);
	if (idx >= MAX_RT_PRIO)
		return NULL;

	head = array->queue + idx;
	curr = head->prev;

	p = list_entry(curr, struct task_struct, run_list);

	curr = curr->prev;

	rq->rt.rt_load_balance_idx = idx;
	rq->rt.rt_load_balance_head = head;
	rq->rt.rt_load_balance_curr = curr;

	return p;
}

static struct task_struct *load_balance_next_rt(void *arg)
{
	struct rq *rq = arg;
	struct rt_prio_array *array = &rq->rt.active;
	struct list_head *head, *curr;
	struct task_struct *p;
	int idx;

	idx = rq->rt.rt_load_balance_idx;
	head = rq->rt.rt_load_balance_head;
	curr = rq->rt.rt_load_balance_curr;

	/*
	 * If we arrived back to the head again then
	 * iterate to the next queue (if any):
	 */
	if (unlikely(head == curr)) {
		int next_idx = find_next_bit(array->bitmap, MAX_RT_PRIO, idx+1);

		if (next_idx >= MAX_RT_PRIO)
			return NULL;

		idx = next_idx;
		head = array->queue + idx;
		curr = head->prev;

		rq->rt.rt_load_balance_idx = idx;
		rq->rt.rt_load_balance_head = head;
	}

	p = list_entry(curr, struct task_struct, run_list);

	curr = curr->prev;

	rq->rt.rt_load_balance_curr = curr;

	return p;
}

static int
load_balance_rt(struct rq *this_rq, int this_cpu, struct rq *busiest,
			unsigned long max_nr_move, unsigned long max_load_move,
			struct sched_domain *sd, enum cpu_idle_type idle,
			int *all_pinned, unsigned long *load_moved)
{
	int this_best_prio, best_prio, best_prio_seen = 0;
	int nr_moved;
	struct rq_iterator rt_rq_iterator;

	best_prio = sched_find_first_bit(busiest->rt.active.bitmap);
	this_best_prio = sched_find_first_bit(this_rq->rt.active.bitmap);

	/*
	 * Enable handling of the case where there is more than one task
	 * with the best priority.   If the current running task is one
	 * of those with prio==best_prio we know it won't be moved
	 * and therefore it's safe to override the skip (based on load)
	 * of any task we find with that prio.
	 */
	if (busiest->curr->prio == best_prio)
		best_prio_seen = 1;

	rt_rq_iterator.start = load_balance_start_rt;
	rt_rq_iterator.next = load_balance_next_rt;
	/* pass 'busiest' rq argument into
	 * load_balance_[start|next]_rt iterators
	 */
	rt_rq_iterator.arg = busiest;

	nr_moved = balance_tasks(this_rq, this_cpu, busiest, max_nr_move,
			max_load_move, sd, idle, all_pinned, load_moved,
			this_best_prio, best_prio, best_prio_seen,
			&rt_rq_iterator);

	return nr_moved;
}

static void task_tick_rt(struct rq *rq, struct task_struct *p)
{
	/*
	 * RR tasks need a special form of timeslice management.
	 * FIFO tasks have no timeslices.
	 */
	if (p->policy != SCHED_RR)
		return;

	if (--p->time_slice)
		return;

	p->time_slice = static_prio_timeslice(p->static_prio);
	set_tsk_need_resched(p);

	/* put it at the end of the queue: */
	requeue_task_rt(rq, p);
}

/*
 * No parent/child timeslice management necessary for RT tasks,
 * just activate them:
 */
static void task_new_rt(struct rq *rq, struct task_struct *p)
{
	activate_task(rq, p, 1);
}

static struct sched_class rt_sched_class __read_mostly = {
	.enqueue_task		= enqueue_task_rt,
	.dequeue_task		= dequeue_task_rt,
	.yield_task		= yield_task_rt,

	.check_preempt_curr	= check_preempt_curr_rt,

	.pick_next_task		= pick_next_task_rt,
	.put_prev_task		= put_prev_task_rt,

	.load_balance		= load_balance_rt,

	.task_tick		= task_tick_rt,
	.task_new		= task_new_rt,
};
