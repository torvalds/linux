/* SPDX-License-Identifier: GPL-2.0 */

#ifdef CONFIG_SCHEDSTATS

/*
 * Expects runqueue lock to be held for atomicity of update
 */
static inline void
rq_sched_info_arrive(struct rq *rq, unsigned long long delta)
{
	if (rq) {
		rq->rq_sched_info.run_delay += delta;
		rq->rq_sched_info.pcount++;
	}
}

/*
 * Expects runqueue lock to be held for atomicity of update
 */
static inline void
rq_sched_info_depart(struct rq *rq, unsigned long long delta)
{
	if (rq)
		rq->rq_cpu_time += delta;
}

static inline void
rq_sched_info_dequeued(struct rq *rq, unsigned long long delta)
{
	if (rq)
		rq->rq_sched_info.run_delay += delta;
}
#define schedstat_enabled()		static_branch_unlikely(&sched_schedstats)
#define __schedstat_inc(var)		do { var++; } while (0)
#define schedstat_inc(var)		do { if (schedstat_enabled()) { var++; } } while (0)
#define __schedstat_add(var, amt)	do { var += (amt); } while (0)
#define schedstat_add(var, amt)		do { if (schedstat_enabled()) { var += (amt); } } while (0)
#define __schedstat_set(var, val)		do { var = (val); } while (0)
#define schedstat_set(var, val)		do { if (schedstat_enabled()) { var = (val); } } while (0)
#define schedstat_val(var)		(var)
#define schedstat_val_or_zero(var)	((schedstat_enabled()) ? (var) : 0)

#else /* !CONFIG_SCHEDSTATS */
static inline void
rq_sched_info_arrive(struct rq *rq, unsigned long long delta)
{}
static inline void
rq_sched_info_dequeued(struct rq *rq, unsigned long long delta)
{}
static inline void
rq_sched_info_depart(struct rq *rq, unsigned long long delta)
{}
#define schedstat_enabled()		0
#define __schedstat_inc(var)		do { } while (0)
#define schedstat_inc(var)		do { } while (0)
#define __schedstat_add(var, amt)	do { } while (0)
#define schedstat_add(var, amt)		do { } while (0)
#define __schedstat_set(var, val)	do { } while (0)
#define schedstat_set(var, val)		do { } while (0)
#define schedstat_val(var)		0
#define schedstat_val_or_zero(var)	0
#endif /* CONFIG_SCHEDSTATS */

#ifdef CONFIG_SCHED_INFO
static inline void sched_info_reset_dequeued(struct task_struct *t)
{
	t->sched_info.last_queued = 0;
}

/*
 * We are interested in knowing how long it was from the *first* time a
 * task was queued to the time that it finally hit a cpu, we call this routine
 * from dequeue_task() to account for possible rq->clock skew across cpus. The
 * delta taken on each cpu would annul the skew.
 */
static inline void sched_info_dequeued(struct rq *rq, struct task_struct *t)
{
	unsigned long long now = rq_clock(rq), delta = 0;

	if (unlikely(sched_info_on()))
		if (t->sched_info.last_queued)
			delta = now - t->sched_info.last_queued;
	sched_info_reset_dequeued(t);
	t->sched_info.run_delay += delta;

	rq_sched_info_dequeued(rq, delta);
}

/*
 * Called when a task finally hits the cpu.  We can now calculate how
 * long it was waiting to run.  We also note when it began so that we
 * can keep stats on how long its timeslice is.
 */
static void sched_info_arrive(struct rq *rq, struct task_struct *t)
{
	unsigned long long now = rq_clock(rq), delta = 0;

	if (t->sched_info.last_queued)
		delta = now - t->sched_info.last_queued;
	sched_info_reset_dequeued(t);
	t->sched_info.run_delay += delta;
	t->sched_info.last_arrival = now;
	t->sched_info.pcount++;

	rq_sched_info_arrive(rq, delta);
}

/*
 * This function is only called from enqueue_task(), but also only updates
 * the timestamp if it is already not set.  It's assumed that
 * sched_info_dequeued() will clear that stamp when appropriate.
 */
static inline void sched_info_queued(struct rq *rq, struct task_struct *t)
{
	if (unlikely(sched_info_on()))
		if (!t->sched_info.last_queued)
			t->sched_info.last_queued = rq_clock(rq);
}

/*
 * Called when a process ceases being the active-running process involuntarily
 * due, typically, to expiring its time slice (this may also be called when
 * switching to the idle task).  Now we can calculate how long we ran.
 * Also, if the process is still in the TASK_RUNNING state, call
 * sched_info_queued() to mark that it has now again started waiting on
 * the runqueue.
 */
static inline void sched_info_depart(struct rq *rq, struct task_struct *t)
{
	unsigned long long delta = rq_clock(rq) -
					t->sched_info.last_arrival;

	rq_sched_info_depart(rq, delta);

	if (t->state == TASK_RUNNING)
		sched_info_queued(rq, t);
}

/*
 * Called when tasks are switched involuntarily due, typically, to expiring
 * their time slice.  (This may also be called when switching to or from
 * the idle task.)  We are only called when prev != next.
 */
static inline void
__sched_info_switch(struct rq *rq,
		    struct task_struct *prev, struct task_struct *next)
{
	/*
	 * prev now departs the cpu.  It's not interesting to record
	 * stats about how efficient we were at scheduling the idle
	 * process, however.
	 */
	if (prev != rq->idle)
		sched_info_depart(rq, prev);

	if (next != rq->idle)
		sched_info_arrive(rq, next);
}
static inline void
sched_info_switch(struct rq *rq,
		  struct task_struct *prev, struct task_struct *next)
{
	if (unlikely(sched_info_on()))
		__sched_info_switch(rq, prev, next);
}
#else
#define sched_info_queued(rq, t)		do { } while (0)
#define sched_info_reset_dequeued(t)	do { } while (0)
#define sched_info_dequeued(rq, t)		do { } while (0)
#define sched_info_depart(rq, t)		do { } while (0)
#define sched_info_arrive(rq, next)		do { } while (0)
#define sched_info_switch(rq, t, next)		do { } while (0)
#endif /* CONFIG_SCHED_INFO */
