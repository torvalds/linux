/*
 * Real-Time Scheduling Class (mapped to the SCHED_FIFO and SCHED_RR
 * policies)
 */

#ifdef CONFIG_SMP

static inline int rt_overloaded(struct rq *rq)
{
	return atomic_read(&rq->rd->rto_count);
}

static inline void rt_set_overload(struct rq *rq)
{
	cpu_set(rq->cpu, rq->rd->rto_mask);
	/*
	 * Make sure the mask is visible before we set
	 * the overload count. That is checked to determine
	 * if we should look at the mask. It would be a shame
	 * if we looked at the mask, but the mask was not
	 * updated yet.
	 */
	wmb();
	atomic_inc(&rq->rd->rto_count);
}

static inline void rt_clear_overload(struct rq *rq)
{
	/* the order here really doesn't matter */
	atomic_dec(&rq->rd->rto_count);
	cpu_clear(rq->cpu, rq->rd->rto_mask);
}

static void update_rt_migration(struct rq *rq)
{
	if (rq->rt.rt_nr_migratory && (rq->rt.rt_nr_running > 1)) {
		if (!rq->rt.overloaded) {
			rt_set_overload(rq);
			rq->rt.overloaded = 1;
		}
	} else if (rq->rt.overloaded) {
		rt_clear_overload(rq);
		rq->rt.overloaded = 0;
	}
}
#endif /* CONFIG_SMP */

static inline struct task_struct *rt_task_of(struct sched_rt_entity *rt_se)
{
	return container_of(rt_se, struct task_struct, rt);
}

static inline int on_rt_rq(struct sched_rt_entity *rt_se)
{
	return !list_empty(&rt_se->run_list);
}

#ifdef CONFIG_FAIR_GROUP_SCHED

static inline unsigned int sched_rt_ratio(struct rt_rq *rt_rq)
{
	if (!rt_rq->tg)
		return SCHED_RT_FRAC;

	return rt_rq->tg->rt_ratio;
}

#define for_each_leaf_rt_rq(rt_rq, rq) \
	list_for_each_entry(rt_rq, &rq->leaf_rt_rq_list, leaf_rt_rq_list)

static inline struct rq *rq_of_rt_rq(struct rt_rq *rt_rq)
{
	return rt_rq->rq;
}

static inline struct rt_rq *rt_rq_of_se(struct sched_rt_entity *rt_se)
{
	return rt_se->rt_rq;
}

#define for_each_sched_rt_entity(rt_se) \
	for (; rt_se; rt_se = rt_se->parent)

static inline struct rt_rq *group_rt_rq(struct sched_rt_entity *rt_se)
{
	return rt_se->my_q;
}

static void enqueue_rt_entity(struct sched_rt_entity *rt_se);
static void dequeue_rt_entity(struct sched_rt_entity *rt_se);

static void sched_rt_ratio_enqueue(struct rt_rq *rt_rq)
{
	struct sched_rt_entity *rt_se = rt_rq->rt_se;

	if (rt_se && !on_rt_rq(rt_se) && rt_rq->rt_nr_running) {
		struct task_struct *curr = rq_of_rt_rq(rt_rq)->curr;

		enqueue_rt_entity(rt_se);
		if (rt_rq->highest_prio < curr->prio)
			resched_task(curr);
	}
}

static void sched_rt_ratio_dequeue(struct rt_rq *rt_rq)
{
	struct sched_rt_entity *rt_se = rt_rq->rt_se;

	if (rt_se && on_rt_rq(rt_se))
		dequeue_rt_entity(rt_se);
}

#else

static inline unsigned int sched_rt_ratio(struct rt_rq *rt_rq)
{
	return sysctl_sched_rt_ratio;
}

#define for_each_leaf_rt_rq(rt_rq, rq) \
	for (rt_rq = &rq->rt; rt_rq; rt_rq = NULL)

static inline struct rq *rq_of_rt_rq(struct rt_rq *rt_rq)
{
	return container_of(rt_rq, struct rq, rt);
}

static inline struct rt_rq *rt_rq_of_se(struct sched_rt_entity *rt_se)
{
	struct task_struct *p = rt_task_of(rt_se);
	struct rq *rq = task_rq(p);

	return &rq->rt;
}

#define for_each_sched_rt_entity(rt_se) \
	for (; rt_se; rt_se = NULL)

static inline struct rt_rq *group_rt_rq(struct sched_rt_entity *rt_se)
{
	return NULL;
}

static inline void sched_rt_ratio_enqueue(struct rt_rq *rt_rq)
{
}

static inline void sched_rt_ratio_dequeue(struct rt_rq *rt_rq)
{
}

#endif

static inline int rt_se_prio(struct sched_rt_entity *rt_se)
{
#ifdef CONFIG_FAIR_GROUP_SCHED
	struct rt_rq *rt_rq = group_rt_rq(rt_se);

	if (rt_rq)
		return rt_rq->highest_prio;
#endif

	return rt_task_of(rt_se)->prio;
}

static int sched_rt_ratio_exceeded(struct rt_rq *rt_rq)
{
	unsigned int rt_ratio = sched_rt_ratio(rt_rq);
	u64 period, ratio;

	if (rt_ratio == SCHED_RT_FRAC)
		return 0;

	if (rt_rq->rt_throttled)
		return 1;

	period = (u64)sysctl_sched_rt_period * NSEC_PER_MSEC;
	ratio = (period * rt_ratio) >> SCHED_RT_FRAC_SHIFT;

	if (rt_rq->rt_time > ratio) {
		struct rq *rq = rq_of_rt_rq(rt_rq);

		rq->rt_throttled = 1;
		rt_rq->rt_throttled = 1;

		sched_rt_ratio_dequeue(rt_rq);
		return 1;
	}

	return 0;
}

static void update_sched_rt_period(struct rq *rq)
{
	struct rt_rq *rt_rq;
	u64 period;

	while (rq->clock > rq->rt_period_expire) {
		period = (u64)sysctl_sched_rt_period * NSEC_PER_MSEC;
		rq->rt_period_expire += period;

		for_each_leaf_rt_rq(rt_rq, rq) {
			unsigned long rt_ratio = sched_rt_ratio(rt_rq);
			u64 ratio = (period * rt_ratio) >> SCHED_RT_FRAC_SHIFT;

			rt_rq->rt_time -= min(rt_rq->rt_time, ratio);
			if (rt_rq->rt_throttled) {
				rt_rq->rt_throttled = 0;
				sched_rt_ratio_enqueue(rt_rq);
			}
		}

		rq->rt_throttled = 0;
	}
}

/*
 * Update the current task's runtime statistics. Skip current tasks that
 * are not in our scheduling class.
 */
static void update_curr_rt(struct rq *rq)
{
	struct task_struct *curr = rq->curr;
	struct sched_rt_entity *rt_se = &curr->rt;
	struct rt_rq *rt_rq = rt_rq_of_se(rt_se);
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

	rt_rq->rt_time += delta_exec;
	/*
	 * might make it a tad more accurate:
	 *
	 * update_sched_rt_period(rq);
	 */
	if (sched_rt_ratio_exceeded(rt_rq))
		resched_task(curr);
}

static inline
void inc_rt_tasks(struct sched_rt_entity *rt_se, struct rt_rq *rt_rq)
{
	WARN_ON(!rt_prio(rt_se_prio(rt_se)));
	rt_rq->rt_nr_running++;
#if defined CONFIG_SMP || defined CONFIG_FAIR_GROUP_SCHED
	if (rt_se_prio(rt_se) < rt_rq->highest_prio)
		rt_rq->highest_prio = rt_se_prio(rt_se);
#endif
#ifdef CONFIG_SMP
	if (rt_se->nr_cpus_allowed > 1) {
		struct rq *rq = rq_of_rt_rq(rt_rq);
		rq->rt.rt_nr_migratory++;
	}

	update_rt_migration(rq_of_rt_rq(rt_rq));
#endif
}

static inline
void dec_rt_tasks(struct sched_rt_entity *rt_se, struct rt_rq *rt_rq)
{
	WARN_ON(!rt_prio(rt_se_prio(rt_se)));
	WARN_ON(!rt_rq->rt_nr_running);
	rt_rq->rt_nr_running--;
#if defined CONFIG_SMP || defined CONFIG_FAIR_GROUP_SCHED
	if (rt_rq->rt_nr_running) {
		struct rt_prio_array *array;

		WARN_ON(rt_se_prio(rt_se) < rt_rq->highest_prio);
		if (rt_se_prio(rt_se) == rt_rq->highest_prio) {
			/* recalculate */
			array = &rt_rq->active;
			rt_rq->highest_prio =
				sched_find_first_bit(array->bitmap);
		} /* otherwise leave rq->highest prio alone */
	} else
		rt_rq->highest_prio = MAX_RT_PRIO;
#endif
#ifdef CONFIG_SMP
	if (rt_se->nr_cpus_allowed > 1) {
		struct rq *rq = rq_of_rt_rq(rt_rq);
		rq->rt.rt_nr_migratory--;
	}

	update_rt_migration(rq_of_rt_rq(rt_rq));
#endif /* CONFIG_SMP */
}

static void enqueue_rt_entity(struct sched_rt_entity *rt_se)
{
	struct rt_rq *rt_rq = rt_rq_of_se(rt_se);
	struct rt_prio_array *array = &rt_rq->active;
	struct rt_rq *group_rq = group_rt_rq(rt_se);

	if (group_rq && group_rq->rt_throttled)
		return;

	list_add_tail(&rt_se->run_list, array->queue + rt_se_prio(rt_se));
	__set_bit(rt_se_prio(rt_se), array->bitmap);

	inc_rt_tasks(rt_se, rt_rq);
}

static void dequeue_rt_entity(struct sched_rt_entity *rt_se)
{
	struct rt_rq *rt_rq = rt_rq_of_se(rt_se);
	struct rt_prio_array *array = &rt_rq->active;

	list_del_init(&rt_se->run_list);
	if (list_empty(array->queue + rt_se_prio(rt_se)))
		__clear_bit(rt_se_prio(rt_se), array->bitmap);

	dec_rt_tasks(rt_se, rt_rq);
}

/*
 * Because the prio of an upper entry depends on the lower
 * entries, we must remove entries top - down.
 *
 * XXX: O(1/2 h^2) because we can only walk up, not down the chain.
 *      doesn't matter much for now, as h=2 for GROUP_SCHED.
 */
static void dequeue_rt_stack(struct task_struct *p)
{
	struct sched_rt_entity *rt_se, *top_se;

	/*
	 * dequeue all, top - down.
	 */
	do {
		rt_se = &p->rt;
		top_se = NULL;
		for_each_sched_rt_entity(rt_se) {
			if (on_rt_rq(rt_se))
				top_se = rt_se;
		}
		if (top_se)
			dequeue_rt_entity(top_se);
	} while (top_se);
}

/*
 * Adding/removing a task to/from a priority array:
 */
static void enqueue_task_rt(struct rq *rq, struct task_struct *p, int wakeup)
{
	struct sched_rt_entity *rt_se = &p->rt;

	if (wakeup)
		rt_se->timeout = 0;

	dequeue_rt_stack(p);

	/*
	 * enqueue everybody, bottom - up.
	 */
	for_each_sched_rt_entity(rt_se)
		enqueue_rt_entity(rt_se);

	inc_cpu_load(rq, p->se.load.weight);
}

static void dequeue_task_rt(struct rq *rq, struct task_struct *p, int sleep)
{
	struct sched_rt_entity *rt_se = &p->rt;
	struct rt_rq *rt_rq;

	update_curr_rt(rq);

	dequeue_rt_stack(p);

	/*
	 * re-enqueue all non-empty rt_rq entities.
	 */
	for_each_sched_rt_entity(rt_se) {
		rt_rq = group_rt_rq(rt_se);
		if (rt_rq && rt_rq->rt_nr_running)
			enqueue_rt_entity(rt_se);
	}

	dec_cpu_load(rq, p->se.load.weight);
}

/*
 * Put task to the end of the run list without the overhead of dequeue
 * followed by enqueue.
 */
static
void requeue_rt_entity(struct rt_rq *rt_rq, struct sched_rt_entity *rt_se)
{
	struct rt_prio_array *array = &rt_rq->active;

	list_move_tail(&rt_se->run_list, array->queue + rt_se_prio(rt_se));
}

static void requeue_task_rt(struct rq *rq, struct task_struct *p)
{
	struct sched_rt_entity *rt_se = &p->rt;
	struct rt_rq *rt_rq;

	for_each_sched_rt_entity(rt_se) {
		rt_rq = rt_rq_of_se(rt_se);
		requeue_rt_entity(rt_rq, rt_se);
	}
}

static void yield_task_rt(struct rq *rq)
{
	requeue_task_rt(rq, rq->curr);
}

#ifdef CONFIG_SMP
static int find_lowest_rq(struct task_struct *task);

static int select_task_rq_rt(struct task_struct *p, int sync)
{
	struct rq *rq = task_rq(p);

	/*
	 * If the current task is an RT task, then
	 * try to see if we can wake this RT task up on another
	 * runqueue. Otherwise simply start this RT task
	 * on its current runqueue.
	 *
	 * We want to avoid overloading runqueues. Even if
	 * the RT task is of higher priority than the current RT task.
	 * RT tasks behave differently than other tasks. If
	 * one gets preempted, we try to push it off to another queue.
	 * So trying to keep a preempting RT task on the same
	 * cache hot CPU will force the running RT task to
	 * a cold CPU. So we waste all the cache for the lower
	 * RT task in hopes of saving some of a RT task
	 * that is just being woken and probably will have
	 * cold cache anyway.
	 */
	if (unlikely(rt_task(rq->curr)) &&
	    (p->rt.nr_cpus_allowed > 1)) {
		int cpu = find_lowest_rq(p);

		return (cpu == -1) ? task_cpu(p) : cpu;
	}

	/*
	 * Otherwise, just let it ride on the affined RQ and the
	 * post-schedule router will push the preempted task away
	 */
	return task_cpu(p);
}
#endif /* CONFIG_SMP */

/*
 * Preempt the current task with a newly woken task if needed:
 */
static void check_preempt_curr_rt(struct rq *rq, struct task_struct *p)
{
	if (p->prio < rq->curr->prio)
		resched_task(rq->curr);
}

static struct sched_rt_entity *pick_next_rt_entity(struct rq *rq,
						   struct rt_rq *rt_rq)
{
	struct rt_prio_array *array = &rt_rq->active;
	struct sched_rt_entity *next = NULL;
	struct list_head *queue;
	int idx;

	idx = sched_find_first_bit(array->bitmap);
	BUG_ON(idx >= MAX_RT_PRIO);

	queue = array->queue + idx;
	next = list_entry(queue->next, struct sched_rt_entity, run_list);

	return next;
}

static struct task_struct *pick_next_task_rt(struct rq *rq)
{
	struct sched_rt_entity *rt_se;
	struct task_struct *p;
	struct rt_rq *rt_rq;

	rt_rq = &rq->rt;

	if (unlikely(!rt_rq->rt_nr_running))
		return NULL;

	if (sched_rt_ratio_exceeded(rt_rq))
		return NULL;

	do {
		rt_se = pick_next_rt_entity(rq, rt_rq);
		BUG_ON(!rt_se);
		rt_rq = group_rt_rq(rt_se);
	} while (rt_rq);

	p = rt_task_of(rt_se);
	p->se.exec_start = rq->clock;
	return p;
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
	    (cpu < 0 || cpu_isset(cpu, p->cpus_allowed)) &&
	    (p->rt.nr_cpus_allowed > 1))
		return 1;
	return 0;
}

/* Return the second highest RT task, NULL otherwise */
static struct task_struct *pick_next_highest_task_rt(struct rq *rq, int cpu)
{
	struct task_struct *next = NULL;
	struct sched_rt_entity *rt_se;
	struct rt_prio_array *array;
	struct rt_rq *rt_rq;
	int idx;

	for_each_leaf_rt_rq(rt_rq, rq) {
		array = &rt_rq->active;
		idx = sched_find_first_bit(array->bitmap);
 next_idx:
		if (idx >= MAX_RT_PRIO)
			continue;
		if (next && next->prio < idx)
			continue;
		list_for_each_entry(rt_se, array->queue + idx, run_list) {
			struct task_struct *p = rt_task_of(rt_se);
			if (pick_rt_task(rq, p, cpu)) {
				next = p;
				break;
			}
		}
		if (!next) {
			idx = find_next_bit(array->bitmap, MAX_RT_PRIO, idx+1);
			goto next_idx;
		}
	}

	return next;
}

static DEFINE_PER_CPU(cpumask_t, local_cpu_mask);

static int find_lowest_cpus(struct task_struct *task, cpumask_t *lowest_mask)
{
	int       lowest_prio = -1;
	int       lowest_cpu  = -1;
	int       count       = 0;
	int       cpu;

	cpus_and(*lowest_mask, task_rq(task)->rd->online, task->cpus_allowed);

	/*
	 * Scan each rq for the lowest prio.
	 */
	for_each_cpu_mask(cpu, *lowest_mask) {
		struct rq *rq = cpu_rq(cpu);

		/* We look for lowest RT prio or non-rt CPU */
		if (rq->rt.highest_prio >= MAX_RT_PRIO) {
			/*
			 * if we already found a low RT queue
			 * and now we found this non-rt queue
			 * clear the mask and set our bit.
			 * Otherwise just return the queue as is
			 * and the count==1 will cause the algorithm
			 * to use the first bit found.
			 */
			if (lowest_cpu != -1) {
				cpus_clear(*lowest_mask);
				cpu_set(rq->cpu, *lowest_mask);
			}
			return 1;
		}

		/* no locking for now */
		if ((rq->rt.highest_prio > task->prio)
		    && (rq->rt.highest_prio >= lowest_prio)) {
			if (rq->rt.highest_prio > lowest_prio) {
				/* new low - clear old data */
				lowest_prio = rq->rt.highest_prio;
				lowest_cpu = cpu;
				count = 0;
			}
			count++;
		} else
			cpu_clear(cpu, *lowest_mask);
	}

	/*
	 * Clear out all the set bits that represent
	 * runqueues that were of higher prio than
	 * the lowest_prio.
	 */
	if (lowest_cpu > 0) {
		/*
		 * Perhaps we could add another cpumask op to
		 * zero out bits. Like cpu_zero_bits(cpumask, nrbits);
		 * Then that could be optimized to use memset and such.
		 */
		for_each_cpu_mask(cpu, *lowest_mask) {
			if (cpu >= lowest_cpu)
				break;
			cpu_clear(cpu, *lowest_mask);
		}
	}

	return count;
}

static inline int pick_optimal_cpu(int this_cpu, cpumask_t *mask)
{
	int first;

	/* "this_cpu" is cheaper to preempt than a remote processor */
	if ((this_cpu != -1) && cpu_isset(this_cpu, *mask))
		return this_cpu;

	first = first_cpu(*mask);
	if (first != NR_CPUS)
		return first;

	return -1;
}

static int find_lowest_rq(struct task_struct *task)
{
	struct sched_domain *sd;
	cpumask_t *lowest_mask = &__get_cpu_var(local_cpu_mask);
	int this_cpu = smp_processor_id();
	int cpu      = task_cpu(task);
	int count    = find_lowest_cpus(task, lowest_mask);

	if (!count)
		return -1; /* No targets found */

	/*
	 * There is no sense in performing an optimal search if only one
	 * target is found.
	 */
	if (count == 1)
		return first_cpu(*lowest_mask);

	/*
	 * At this point we have built a mask of cpus representing the
	 * lowest priority tasks in the system.  Now we want to elect
	 * the best one based on our affinity and topology.
	 *
	 * We prioritize the last cpu that the task executed on since
	 * it is most likely cache-hot in that location.
	 */
	if (cpu_isset(cpu, *lowest_mask))
		return cpu;

	/*
	 * Otherwise, we consult the sched_domains span maps to figure
	 * out which cpu is logically closest to our hot cache data.
	 */
	if (this_cpu == cpu)
		this_cpu = -1; /* Skip this_cpu opt if the same */

	for_each_domain(cpu, sd) {
		if (sd->flags & SD_WAKE_AFFINE) {
			cpumask_t domain_mask;
			int       best_cpu;

			cpus_and(domain_mask, sd->span, *lowest_mask);

			best_cpu = pick_optimal_cpu(this_cpu,
						    &domain_mask);
			if (best_cpu != -1)
				return best_cpu;
		}
	}

	/*
	 * And finally, if there were no matches within the domains
	 * just give the caller *something* to work with from the compatible
	 * locations.
	 */
	return pick_optimal_cpu(this_cpu, lowest_mask);
}

/* Will lock the rq it finds */
static struct rq *find_lock_lowest_rq(struct task_struct *task, struct rq *rq)
{
	struct rq *lowest_rq = NULL;
	int tries;
	int cpu;

	for (tries = 0; tries < RT_MAX_TRIES; tries++) {
		cpu = find_lowest_rq(task);

		if ((cpu == -1) || (cpu == rq->cpu))
			break;

		lowest_rq = cpu_rq(cpu);

		/* if the prio of this runqueue changed, try again */
		if (double_lock_balance(rq, lowest_rq)) {
			/*
			 * We had to unlock the run queue. In
			 * the mean time, task could have
			 * migrated already or had its affinity changed.
			 * Also make sure that it wasn't scheduled on its rq.
			 */
			if (unlikely(task_rq(task) != rq ||
				     !cpu_isset(lowest_rq->cpu,
						task->cpus_allowed) ||
				     task_running(rq, task) ||
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
static int push_rt_task(struct rq *rq)
{
	struct task_struct *next_task;
	struct rq *lowest_rq;
	int ret = 0;
	int paranoid = RT_MAX_TRIES;

	if (!rq->rt.overloaded)
		return 0;

	next_task = pick_next_highest_task_rt(rq, -1);
	if (!next_task)
		return 0;

 retry:
	if (unlikely(next_task == rq->curr)) {
		WARN_ON(1);
		return 0;
	}

	/*
	 * It's possible that the next_task slipped in of
	 * higher priority than current. If that's the case
	 * just reschedule current.
	 */
	if (unlikely(next_task->prio < rq->curr->prio)) {
		resched_task(rq->curr);
		return 0;
	}

	/* We might release rq lock */
	get_task_struct(next_task);

	/* find_lock_lowest_rq locks the rq if found */
	lowest_rq = find_lock_lowest_rq(next_task, rq);
	if (!lowest_rq) {
		struct task_struct *task;
		/*
		 * find lock_lowest_rq releases rq->lock
		 * so it is possible that next_task has changed.
		 * If it has, then try again.
		 */
		task = pick_next_highest_task_rt(rq, -1);
		if (unlikely(task != next_task) && task && paranoid--) {
			put_task_struct(next_task);
			next_task = task;
			goto retry;
		}
		goto out;
	}

	deactivate_task(rq, next_task, 0);
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
	int this_cpu = this_rq->cpu, ret = 0, cpu;
	struct task_struct *p, *next;
	struct rq *src_rq;

	if (likely(!rt_overloaded(this_rq)))
		return 0;

	next = pick_next_task_rt(this_rq);

	for_each_cpu_mask(cpu, this_rq->rd->rto_mask) {
		if (this_cpu == cpu)
			continue;

		src_rq = cpu_rq(cpu);
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
		if (src_rq->rt.rt_nr_running <= 1)
			goto skip;

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
				goto skip;

			ret = 1;

			deactivate_task(src_rq, p, 0);
			set_task_cpu(p, this_cpu);
			activate_task(this_rq, p, 0);
			/*
			 * We continue with the search, just in
			 * case there's an even higher prio task
			 * in another runqueue. (low likelyhood
			 * but possible)
			 *
			 * Update next so that we won't pick a task
			 * on another cpu with a priority lower (or equal)
			 * than the one we just picked.
			 */
			next = p;

		}
 skip:
		spin_unlock(&src_rq->lock);
	}

	return ret;
}

static void pre_schedule_rt(struct rq *rq, struct task_struct *prev)
{
	/* Try to pull RT tasks here if we lower this rq's prio */
	if (unlikely(rt_task(prev)) && rq->rt.highest_prio > prev->prio)
		pull_rt_task(rq);
}

static void post_schedule_rt(struct rq *rq)
{
	/*
	 * If we have more than one rt_task queued, then
	 * see if we can push the other rt_tasks off to other CPUS.
	 * Note we may release the rq lock, and since
	 * the lock was owned by prev, we need to release it
	 * first via finish_lock_switch and then reaquire it here.
	 */
	if (unlikely(rq->rt.overloaded)) {
		spin_lock_irq(&rq->lock);
		push_rt_tasks(rq);
		spin_unlock_irq(&rq->lock);
	}
}


static void task_wake_up_rt(struct rq *rq, struct task_struct *p)
{
	if (!task_running(rq, p) &&
	    (p->prio >= rq->rt.highest_prio) &&
	    rq->rt.overloaded)
		push_rt_tasks(rq);
}

static unsigned long
load_balance_rt(struct rq *this_rq, int this_cpu, struct rq *busiest,
		unsigned long max_load_move,
		struct sched_domain *sd, enum cpu_idle_type idle,
		int *all_pinned, int *this_best_prio)
{
	/* don't touch RT tasks */
	return 0;
}

static int
move_one_task_rt(struct rq *this_rq, int this_cpu, struct rq *busiest,
		 struct sched_domain *sd, enum cpu_idle_type idle)
{
	/* don't touch RT tasks */
	return 0;
}

static void set_cpus_allowed_rt(struct task_struct *p, cpumask_t *new_mask)
{
	int weight = cpus_weight(*new_mask);

	BUG_ON(!rt_task(p));

	/*
	 * Update the migration status of the RQ if we have an RT task
	 * which is running AND changing its weight value.
	 */
	if (p->se.on_rq && (weight != p->rt.nr_cpus_allowed)) {
		struct rq *rq = task_rq(p);

		if ((p->rt.nr_cpus_allowed <= 1) && (weight > 1)) {
			rq->rt.rt_nr_migratory++;
		} else if ((p->rt.nr_cpus_allowed > 1) && (weight <= 1)) {
			BUG_ON(!rq->rt.rt_nr_migratory);
			rq->rt.rt_nr_migratory--;
		}

		update_rt_migration(rq);
	}

	p->cpus_allowed    = *new_mask;
	p->rt.nr_cpus_allowed = weight;
}

/* Assumes rq->lock is held */
static void join_domain_rt(struct rq *rq)
{
	if (rq->rt.overloaded)
		rt_set_overload(rq);
}

/* Assumes rq->lock is held */
static void leave_domain_rt(struct rq *rq)
{
	if (rq->rt.overloaded)
		rt_clear_overload(rq);
}

/*
 * When switch from the rt queue, we bring ourselves to a position
 * that we might want to pull RT tasks from other runqueues.
 */
static void switched_from_rt(struct rq *rq, struct task_struct *p,
			   int running)
{
	/*
	 * If there are other RT tasks then we will reschedule
	 * and the scheduling of the other RT tasks will handle
	 * the balancing. But if we are the last RT task
	 * we may need to handle the pulling of RT tasks
	 * now.
	 */
	if (!rq->rt.rt_nr_running)
		pull_rt_task(rq);
}
#endif /* CONFIG_SMP */

/*
 * When switching a task to RT, we may overload the runqueue
 * with RT tasks. In this case we try to push them off to
 * other runqueues.
 */
static void switched_to_rt(struct rq *rq, struct task_struct *p,
			   int running)
{
	int check_resched = 1;

	/*
	 * If we are already running, then there's nothing
	 * that needs to be done. But if we are not running
	 * we may need to preempt the current running task.
	 * If that current running task is also an RT task
	 * then see if we can move to another run queue.
	 */
	if (!running) {
#ifdef CONFIG_SMP
		if (rq->rt.overloaded && push_rt_task(rq) &&
		    /* Don't resched if we changed runqueues */
		    rq != task_rq(p))
			check_resched = 0;
#endif /* CONFIG_SMP */
		if (check_resched && p->prio < rq->curr->prio)
			resched_task(rq->curr);
	}
}

/*
 * Priority of the task has changed. This may cause
 * us to initiate a push or pull.
 */
static void prio_changed_rt(struct rq *rq, struct task_struct *p,
			    int oldprio, int running)
{
	if (running) {
#ifdef CONFIG_SMP
		/*
		 * If our priority decreases while running, we
		 * may need to pull tasks to this runqueue.
		 */
		if (oldprio < p->prio)
			pull_rt_task(rq);
		/*
		 * If there's a higher priority task waiting to run
		 * then reschedule.
		 */
		if (p->prio > rq->rt.highest_prio)
			resched_task(p);
#else
		/* For UP simply resched on drop of prio */
		if (oldprio < p->prio)
			resched_task(p);
#endif /* CONFIG_SMP */
	} else {
		/*
		 * This task is not running, but if it is
		 * greater than the current running task
		 * then reschedule.
		 */
		if (p->prio < rq->curr->prio)
			resched_task(rq->curr);
	}
}

static void watchdog(struct rq *rq, struct task_struct *p)
{
	unsigned long soft, hard;

	if (!p->signal)
		return;

	soft = p->signal->rlim[RLIMIT_RTTIME].rlim_cur;
	hard = p->signal->rlim[RLIMIT_RTTIME].rlim_max;

	if (soft != RLIM_INFINITY) {
		unsigned long next;

		p->rt.timeout++;
		next = DIV_ROUND_UP(min(soft, hard), USEC_PER_SEC/HZ);
		if (p->rt.timeout > next)
			p->it_sched_expires = p->se.sum_exec_runtime;
	}
}

static void task_tick_rt(struct rq *rq, struct task_struct *p, int queued)
{
	update_curr_rt(rq);

	watchdog(rq, p);

	/*
	 * RR tasks need a special form of timeslice management.
	 * FIFO tasks have no timeslices.
	 */
	if (p->policy != SCHED_RR)
		return;

	if (--p->rt.time_slice)
		return;

	p->rt.time_slice = DEF_TIMESLICE;

	/*
	 * Requeue to the end of queue if we are not the only element
	 * on the queue:
	 */
	if (p->rt.run_list.prev != p->rt.run_list.next) {
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
#ifdef CONFIG_SMP
	.select_task_rq		= select_task_rq_rt,
#endif /* CONFIG_SMP */

	.check_preempt_curr	= check_preempt_curr_rt,

	.pick_next_task		= pick_next_task_rt,
	.put_prev_task		= put_prev_task_rt,

#ifdef CONFIG_SMP
	.load_balance		= load_balance_rt,
	.move_one_task		= move_one_task_rt,
	.set_cpus_allowed       = set_cpus_allowed_rt,
	.join_domain            = join_domain_rt,
	.leave_domain           = leave_domain_rt,
	.pre_schedule		= pre_schedule_rt,
	.post_schedule		= post_schedule_rt,
	.task_wake_up		= task_wake_up_rt,
	.switched_from		= switched_from_rt,
#endif

	.set_curr_task          = set_curr_task_rt,
	.task_tick		= task_tick_rt,

	.prio_changed		= prio_changed_rt,
	.switched_to		= switched_to_rt,
};
