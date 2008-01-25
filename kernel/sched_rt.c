/*
 * Real-Time Scheduling Class (mapped to the SCHED_FIFO and SCHED_RR
 * policies)
 */

#ifdef CONFIG_SMP
static cpumask_t rt_overload_mask;
static atomic_t rto_count;
static inline int rt_overloaded(void)
{
	return atomic_read(&rto_count);
}
static inline cpumask_t *rt_overload(void)
{
	return &rt_overload_mask;
}
static inline void rt_set_overload(struct rq *rq)
{
	cpu_set(rq->cpu, rt_overload_mask);
	/*
	 * Make sure the mask is visible before we set
	 * the overload count. That is checked to determine
	 * if we should look at the mask. It would be a shame
	 * if we looked at the mask, but the mask was not
	 * updated yet.
	 */
	wmb();
	atomic_inc(&rto_count);
}
static inline void rt_clear_overload(struct rq *rq)
{
	/* the order here really doesn't matter */
	atomic_dec(&rto_count);
	cpu_clear(rq->cpu, rt_overload_mask);
}
#endif /* CONFIG_SMP */

/*
 * Update the current task's runtime statistics. Skip current tasks that
 * are not in our scheduling class.
 */
static void update_curr_rt(struct rq *rq)
{
	struct task_struct *curr = rq->curr;
	u64 delta_exec;

	if (!task_has_rt_policy(curr))
		return;

	delta_exec = rq->clock - curr->se.exec_start;
	if (unlikely((s64)delta_exec < 0))
		delta_exec = 0;

	schedstat_set(curr->se.exec_max, max(curr->se.exec_max, delta_exec));

	curr->se.sum_exec_runtime += delta_exec;
	curr->se.exec_start = rq->clock;
	cpuacct_charge(curr, delta_exec);
}

static inline void inc_rt_tasks(struct task_struct *p, struct rq *rq)
{
	WARN_ON(!rt_task(p));
	rq->rt.rt_nr_running++;
#ifdef CONFIG_SMP
	if (p->prio < rq->rt.highest_prio)
		rq->rt.highest_prio = p->prio;
	if (rq->rt.rt_nr_running > 1)
		rt_set_overload(rq);
#endif /* CONFIG_SMP */
}

static inline void dec_rt_tasks(struct task_struct *p, struct rq *rq)
{
	WARN_ON(!rt_task(p));
	WARN_ON(!rq->rt.rt_nr_running);
	rq->rt.rt_nr_running--;
#ifdef CONFIG_SMP
	if (rq->rt.rt_nr_running) {
		struct rt_prio_array *array;

		WARN_ON(p->prio < rq->rt.highest_prio);
		if (p->prio == rq->rt.highest_prio) {
			/* recalculate */
			array = &rq->rt.active;
			rq->rt.highest_prio =
				sched_find_first_bit(array->bitmap);
		} /* otherwise leave rq->highest prio alone */
	} else
		rq->rt.highest_prio = MAX_RT_PRIO;
	if (rq->rt.rt_nr_running < 2)
		rt_clear_overload(rq);
#endif /* CONFIG_SMP */
}

static void enqueue_task_rt(struct rq *rq, struct task_struct *p, int wakeup)
{
	struct rt_prio_array *array = &rq->rt.active;

	list_add_tail(&p->run_list, array->queue + p->prio);
	__set_bit(p->prio, array->bitmap);
	inc_cpu_load(rq, p->se.load.weight);

	inc_rt_tasks(p, rq);
}

/*
 * Adding/removing a task to/from a priority array:
 */
static void dequeue_task_rt(struct rq *rq, struct task_struct *p, int sleep)
{
	struct rt_prio_array *array = &rq->rt.active;

	update_curr_rt(rq);

	list_del(&p->run_list);
	if (list_empty(array->queue + p->prio))
		__clear_bit(p->prio, array->bitmap);
	dec_cpu_load(rq, p->se.load.weight);

	dec_rt_tasks(p, rq);
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
yield_task_rt(struct rq *rq)
{
	requeue_task_rt(rq, rq->curr);
}

/*
 * Preempt the current task with a newly woken task if needed:
 */
static void check_preempt_curr_rt(struct rq *rq, struct task_struct *p)
{
	if (p->prio < rq->curr->prio)
		resched_task(rq->curr);
}

static struct task_struct *pick_next_task_rt(struct rq *rq)
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

	next->se.exec_start = rq->clock;

	return next;
}

static void put_prev_task_rt(struct rq *rq, struct task_struct *p)
{
	update_curr_rt(rq);
	p->se.exec_start = 0;
}

#ifdef CONFIG_SMP
/* Only try algorithms three times */
#define RT_MAX_TRIES 3

static int double_lock_balance(struct rq *this_rq, struct rq *busiest);
static void deactivate_task(struct rq *rq, struct task_struct *p, int sleep);

static int pick_rt_task(struct rq *rq, struct task_struct *p, int cpu)
{
	if (!task_running(rq, p) &&
	    (cpu < 0 || cpu_isset(cpu, p->cpus_allowed)))
		return 1;
	return 0;
}

/* Return the second highest RT task, NULL otherwise */
static struct task_struct *pick_next_highest_task_rt(struct rq *rq,
						     int cpu)
{
	struct rt_prio_array *array = &rq->rt.active;
	struct task_struct *next;
	struct list_head *queue;
	int idx;

	assert_spin_locked(&rq->lock);

	if (likely(rq->rt.rt_nr_running < 2))
		return NULL;

	idx = sched_find_first_bit(array->bitmap);
	if (unlikely(idx >= MAX_RT_PRIO)) {
		WARN_ON(1); /* rt_nr_running is bad */
		return NULL;
	}

	queue = array->queue + idx;
	BUG_ON(list_empty(queue));

	next = list_entry(queue->next, struct task_struct, run_list);
	if (unlikely(pick_rt_task(rq, next, cpu)))
		goto out;

	if (queue->next->next != queue) {
		/* same prio task */
		next = list_entry(queue->next->next, struct task_struct, run_list);
		if (pick_rt_task(rq, next, cpu))
			goto out;
	}

 retry:
	/* slower, but more flexible */
	idx = find_next_bit(array->bitmap, MAX_RT_PRIO, idx+1);
	if (unlikely(idx >= MAX_RT_PRIO))
		return NULL;

	queue = array->queue + idx;
	BUG_ON(list_empty(queue));

	list_for_each_entry(next, queue, run_list) {
		if (pick_rt_task(rq, next, cpu))
			goto out;
	}

	goto retry;

 out:
	return next;
}

static DEFINE_PER_CPU(cpumask_t, local_cpu_mask);

/* Will lock the rq it finds */
static struct rq *find_lock_lowest_rq(struct task_struct *task,
				      struct rq *this_rq)
{
	struct rq *lowest_rq = NULL;
	int cpu;
	int tries;
	cpumask_t *cpu_mask = &__get_cpu_var(local_cpu_mask);

	cpus_and(*cpu_mask, cpu_online_map, task->cpus_allowed);

	for (tries = 0; tries < RT_MAX_TRIES; tries++) {
		/*
		 * Scan each rq for the lowest prio.
		 */
		for_each_cpu_mask(cpu, *cpu_mask) {
			struct rq *rq = &per_cpu(runqueues, cpu);

			if (cpu == this_rq->cpu)
				continue;

			/* We look for lowest RT prio or non-rt CPU */
			if (rq->rt.highest_prio >= MAX_RT_PRIO) {
				lowest_rq = rq;
				break;
			}

			/* no locking for now */
			if (rq->rt.highest_prio > task->prio &&
			    (!lowest_rq || rq->rt.highest_prio > lowest_rq->rt.highest_prio)) {
				lowest_rq = rq;
			}
		}

		if (!lowest_rq)
			break;

		/* if the prio of this runqueue changed, try again */
		if (double_lock_balance(this_rq, lowest_rq)) {
			/*
			 * We had to unlock the run queue. In
			 * the mean time, task could have
			 * migrated already or had its affinity changed.
			 * Also make sure that it wasn't scheduled on its rq.
			 */
			if (unlikely(task_rq(task) != this_rq ||
				     !cpu_isset(lowest_rq->cpu, task->cpus_allowed) ||
				     task_running(this_rq, task) ||
				     !task->se.on_rq)) {
				spin_unlock(&lowest_rq->lock);
				lowest_rq = NULL;
				break;
			}
		}

		/* If this rq is still suitable use it. */
		if (lowest_rq->rt.highest_prio > task->prio)
			break;

		/* try again */
		spin_unlock(&lowest_rq->lock);
		lowest_rq = NULL;
	}

	return lowest_rq;
}

/*
 * If the current CPU has more than one RT task, see if the non
 * running task can migrate over to a CPU that is running a task
 * of lesser priority.
 */
static int push_rt_task(struct rq *this_rq)
{
	struct task_struct *next_task;
	struct rq *lowest_rq;
	int ret = 0;
	int paranoid = RT_MAX_TRIES;

	assert_spin_locked(&this_rq->lock);

	next_task = pick_next_highest_task_rt(this_rq, -1);
	if (!next_task)
		return 0;

 retry:
	if (unlikely(next_task == this_rq->curr)) {
		WARN_ON(1);
		return 0;
	}

	/*
	 * It's possible that the next_task slipped in of
	 * higher priority than current. If that's the case
	 * just reschedule current.
	 */
	if (unlikely(next_task->prio < this_rq->curr->prio)) {
		resched_task(this_rq->curr);
		return 0;
	}

	/* We might release this_rq lock */
	get_task_struct(next_task);

	/* find_lock_lowest_rq locks the rq if found */
	lowest_rq = find_lock_lowest_rq(next_task, this_rq);
	if (!lowest_rq) {
		struct task_struct *task;
		/*
		 * find lock_lowest_rq releases this_rq->lock
		 * so it is possible that next_task has changed.
		 * If it has, then try again.
		 */
		task = pick_next_highest_task_rt(this_rq, -1);
		if (unlikely(task != next_task) && task && paranoid--) {
			put_task_struct(next_task);
			next_task = task;
			goto retry;
		}
		goto out;
	}

	assert_spin_locked(&lowest_rq->lock);

	deactivate_task(this_rq, next_task, 0);
	set_task_cpu(next_task, lowest_rq->cpu);
	activate_task(lowest_rq, next_task, 0);

	resched_task(lowest_rq->curr);

	spin_unlock(&lowest_rq->lock);

	ret = 1;
out:
	put_task_struct(next_task);

	return ret;
}

/*
 * TODO: Currently we just use the second highest prio task on
 *       the queue, and stop when it can't migrate (or there's
 *       no more RT tasks).  There may be a case where a lower
 *       priority RT task has a different affinity than the
 *       higher RT task. In this case the lower RT task could
 *       possibly be able to migrate where as the higher priority
 *       RT task could not.  We currently ignore this issue.
 *       Enhancements are welcome!
 */
static void push_rt_tasks(struct rq *rq)
{
	/* push_rt_task will return true if it moved an RT */
	while (push_rt_task(rq))
		;
}

static int pull_rt_task(struct rq *this_rq)
{
	struct task_struct *next;
	struct task_struct *p;
	struct rq *src_rq;
	cpumask_t *rto_cpumask;
	int this_cpu = this_rq->cpu;
	int cpu;
	int ret = 0;

	assert_spin_locked(&this_rq->lock);

	/*
	 * If cpusets are used, and we have overlapping
	 * run queue cpusets, then this algorithm may not catch all.
	 * This is just the price you pay on trying to keep
	 * dirtying caches down on large SMP machines.
	 */
	if (likely(!rt_overloaded()))
		return 0;

	next = pick_next_task_rt(this_rq);

	rto_cpumask = rt_overload();

	for_each_cpu_mask(cpu, *rto_cpumask) {
		if (this_cpu == cpu)
			continue;

		src_rq = cpu_rq(cpu);
		if (unlikely(src_rq->rt.rt_nr_running <= 1)) {
			/*
			 * It is possible that overlapping cpusets
			 * will miss clearing a non overloaded runqueue.
			 * Clear it now.
			 */
			if (double_lock_balance(this_rq, src_rq)) {
				/* unlocked our runqueue lock */
				struct task_struct *old_next = next;
				next = pick_next_task_rt(this_rq);
				if (next != old_next)
					ret = 1;
			}
			if (likely(src_rq->rt.rt_nr_running <= 1))
				/*
				 * Small chance that this_rq->curr changed
				 * but it's really harmless here.
				 */
				rt_clear_overload(this_rq);
			else
				/*
				 * Heh, the src_rq is now overloaded, since
				 * we already have the src_rq lock, go straight
				 * to pulling tasks from it.
				 */
				goto try_pulling;
			spin_unlock(&src_rq->lock);
			continue;
		}

		/*
		 * We can potentially drop this_rq's lock in
		 * double_lock_balance, and another CPU could
		 * steal our next task - hence we must cause
		 * the caller to recalculate the next task
		 * in that case:
		 */
		if (double_lock_balance(this_rq, src_rq)) {
			struct task_struct *old_next = next;
			next = pick_next_task_rt(this_rq);
			if (next != old_next)
				ret = 1;
		}

		/*
		 * Are there still pullable RT tasks?
		 */
		if (src_rq->rt.rt_nr_running <= 1) {
			spin_unlock(&src_rq->lock);
			continue;
		}

 try_pulling:
		p = pick_next_highest_task_rt(src_rq, this_cpu);

		/*
		 * Do we have an RT task that preempts
		 * the to-be-scheduled task?
		 */
		if (p && (!next || (p->prio < next->prio))) {
			WARN_ON(p == src_rq->curr);
			WARN_ON(!p->se.on_rq);

			/*
			 * There's a chance that p is higher in priority
			 * than what's currently running on its cpu.
			 * This is just that p is wakeing up and hasn't
			 * had a chance to schedule. We only pull
			 * p if it is lower in priority than the
			 * current task on the run queue or
			 * this_rq next task is lower in prio than
			 * the current task on that rq.
			 */
			if (p->prio < src_rq->curr->prio ||
			    (next && next->prio < src_rq->curr->prio))
				goto bail;

			ret = 1;

			deactivate_task(src_rq, p, 0);
			set_task_cpu(p, this_cpu);
			activate_task(this_rq, p, 0);
			/*
			 * We continue with the search, just in
			 * case there's an even higher prio task
			 * in another runqueue. (low likelyhood
			 * but possible)
			 */

			/*
			 * Update next so that we won't pick a task
			 * on another cpu with a priority lower (or equal)
			 * than the one we just picked.
			 */
			next = p;

		}
 bail:
		spin_unlock(&src_rq->lock);
	}

	return ret;
}

static void schedule_balance_rt(struct rq *rq,
				struct task_struct *prev)
{
	/* Try to pull RT tasks here if we lower this rq's prio */
	if (unlikely(rt_task(prev)) &&
	    rq->rt.highest_prio > prev->prio)
		pull_rt_task(rq);
}

static void schedule_tail_balance_rt(struct rq *rq)
{
	/*
	 * If we have more than one rt_task queued, then
	 * see if we can push the other rt_tasks off to other CPUS.
	 * Note we may release the rq lock, and since
	 * the lock was owned by prev, we need to release it
	 * first via finish_lock_switch and then reaquire it here.
	 */
	if (unlikely(rq->rt.rt_nr_running > 1)) {
		spin_lock_irq(&rq->lock);
		push_rt_tasks(rq);
		spin_unlock_irq(&rq->lock);
	}
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

static unsigned long
load_balance_rt(struct rq *this_rq, int this_cpu, struct rq *busiest,
		unsigned long max_load_move,
		struct sched_domain *sd, enum cpu_idle_type idle,
		int *all_pinned, int *this_best_prio)
{
	struct rq_iterator rt_rq_iterator;

	rt_rq_iterator.start = load_balance_start_rt;
	rt_rq_iterator.next = load_balance_next_rt;
	/* pass 'busiest' rq argument into
	 * load_balance_[start|next]_rt iterators
	 */
	rt_rq_iterator.arg = busiest;

	return balance_tasks(this_rq, this_cpu, busiest, max_load_move, sd,
			     idle, all_pinned, this_best_prio, &rt_rq_iterator);
}

static int
move_one_task_rt(struct rq *this_rq, int this_cpu, struct rq *busiest,
		 struct sched_domain *sd, enum cpu_idle_type idle)
{
	struct rq_iterator rt_rq_iterator;

	rt_rq_iterator.start = load_balance_start_rt;
	rt_rq_iterator.next = load_balance_next_rt;
	rt_rq_iterator.arg = busiest;

	return iter_move_one_task(this_rq, this_cpu, busiest, sd, idle,
				  &rt_rq_iterator);
}
#else /* CONFIG_SMP */
# define schedule_tail_balance_rt(rq)	do { } while (0)
# define schedule_balance_rt(rq, prev)	do { } while (0)
#endif /* CONFIG_SMP */

static void task_tick_rt(struct rq *rq, struct task_struct *p)
{
	update_curr_rt(rq);

	/*
	 * RR tasks need a special form of timeslice management.
	 * FIFO tasks have no timeslices.
	 */
	if (p->policy != SCHED_RR)
		return;

	if (--p->time_slice)
		return;

	p->time_slice = DEF_TIMESLICE;

	/*
	 * Requeue to the end of queue if we are not the only element
	 * on the queue:
	 */
	if (p->run_list.prev != p->run_list.next) {
		requeue_task_rt(rq, p);
		set_tsk_need_resched(p);
	}
}

static void set_curr_task_rt(struct rq *rq)
{
	struct task_struct *p = rq->curr;

	p->se.exec_start = rq->clock;
}

const struct sched_class rt_sched_class = {
	.next			= &fair_sched_class,
	.enqueue_task		= enqueue_task_rt,
	.dequeue_task		= dequeue_task_rt,
	.yield_task		= yield_task_rt,

	.check_preempt_curr	= check_preempt_curr_rt,

	.pick_next_task		= pick_next_task_rt,
	.put_prev_task		= put_prev_task_rt,

#ifdef CONFIG_SMP
	.load_balance		= load_balance_rt,
	.move_one_task		= move_one_task_rt,
#endif

	.set_curr_task          = set_curr_task_rt,
	.task_tick		= task_tick_rt,
};
