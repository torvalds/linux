/*-
 * Copyright (c) 2000 Doug Rabson
 * Copyright (c) 2014 Jeff Roberson
 * Copyright (c) 2016 Matthew Macy
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/bus.h>
#include <sys/cpuset.h>
#include <sys/kernel.h>
#include <sys/kthread.h>
#include <sys/libkern.h>
#include <sys/limits.h>
#include <sys/lock.h>
#include <sys/malloc.h>
#include <sys/mutex.h>
#include <sys/proc.h>
#include <sys/sched.h>
#include <sys/smp.h>
#include <sys/gtaskqueue.h>
#include <sys/unistd.h>
#include <machine/stdarg.h>

static MALLOC_DEFINE(M_GTASKQUEUE, "gtaskqueue", "Group Task Queues");
static void	gtaskqueue_thread_enqueue(void *);
static void	gtaskqueue_thread_loop(void *arg);
static int	task_is_running(struct gtaskqueue *queue, struct gtask *gtask);
static void	gtaskqueue_drain_locked(struct gtaskqueue *queue, struct gtask *gtask);

TASKQGROUP_DEFINE(softirq, mp_ncpus, 1);
TASKQGROUP_DEFINE(config, 1, 1);

struct gtaskqueue_busy {
	struct gtask	*tb_running;
	TAILQ_ENTRY(gtaskqueue_busy) tb_link;
};

static struct gtask * const TB_DRAIN_WAITER = (struct gtask *)0x1;

typedef void (*gtaskqueue_enqueue_fn)(void *context);

struct gtaskqueue {
	STAILQ_HEAD(, gtask)	tq_queue;
	gtaskqueue_enqueue_fn	tq_enqueue;
	void			*tq_context;
	char			*tq_name;
	TAILQ_HEAD(, gtaskqueue_busy) tq_active;
	struct mtx		tq_mutex;
	struct thread		**tq_threads;
	int			tq_tcount;
	int			tq_spin;
	int			tq_flags;
	int			tq_callouts;
	taskqueue_callback_fn	tq_callbacks[TASKQUEUE_NUM_CALLBACKS];
	void			*tq_cb_contexts[TASKQUEUE_NUM_CALLBACKS];
};

#define	TQ_FLAGS_ACTIVE		(1 << 0)
#define	TQ_FLAGS_BLOCKED	(1 << 1)
#define	TQ_FLAGS_UNLOCKED_ENQUEUE	(1 << 2)

#define	DT_CALLOUT_ARMED	(1 << 0)

#define	TQ_LOCK(tq)							\
	do {								\
		if ((tq)->tq_spin)					\
			mtx_lock_spin(&(tq)->tq_mutex);			\
		else							\
			mtx_lock(&(tq)->tq_mutex);			\
	} while (0)
#define	TQ_ASSERT_LOCKED(tq)	mtx_assert(&(tq)->tq_mutex, MA_OWNED)

#define	TQ_UNLOCK(tq)							\
	do {								\
		if ((tq)->tq_spin)					\
			mtx_unlock_spin(&(tq)->tq_mutex);		\
		else							\
			mtx_unlock(&(tq)->tq_mutex);			\
	} while (0)
#define	TQ_ASSERT_UNLOCKED(tq)	mtx_assert(&(tq)->tq_mutex, MA_NOTOWNED)

#ifdef INVARIANTS
static void
gtask_dump(struct gtask *gtask)
{
	printf("gtask: %p ta_flags=%x ta_priority=%d ta_func=%p ta_context=%p\n",
	       gtask, gtask->ta_flags, gtask->ta_priority, gtask->ta_func, gtask->ta_context);
}
#endif

static __inline int
TQ_SLEEP(struct gtaskqueue *tq, void *p, struct mtx *m, int pri, const char *wm,
    int t)
{
	if (tq->tq_spin)
		return (msleep_spin(p, m, wm, t));
	return (msleep(p, m, pri, wm, t));
}

static struct gtaskqueue *
_gtaskqueue_create(const char *name, int mflags,
		 taskqueue_enqueue_fn enqueue, void *context,
		 int mtxflags, const char *mtxname __unused)
{
	struct gtaskqueue *queue;
	char *tq_name;

	tq_name = malloc(TASKQUEUE_NAMELEN, M_GTASKQUEUE, mflags | M_ZERO);
	if (!tq_name)
		return (NULL);

	snprintf(tq_name, TASKQUEUE_NAMELEN, "%s", (name) ? name : "taskqueue");

	queue = malloc(sizeof(struct gtaskqueue), M_GTASKQUEUE, mflags | M_ZERO);
	if (!queue) {
		free(tq_name, M_GTASKQUEUE);
		return (NULL);
	}

	STAILQ_INIT(&queue->tq_queue);
	TAILQ_INIT(&queue->tq_active);
	queue->tq_enqueue = enqueue;
	queue->tq_context = context;
	queue->tq_name = tq_name;
	queue->tq_spin = (mtxflags & MTX_SPIN) != 0;
	queue->tq_flags |= TQ_FLAGS_ACTIVE;
	if (enqueue == gtaskqueue_thread_enqueue)
		queue->tq_flags |= TQ_FLAGS_UNLOCKED_ENQUEUE;
	mtx_init(&queue->tq_mutex, tq_name, NULL, mtxflags);

	return (queue);
}


/*
 * Signal a taskqueue thread to terminate.
 */
static void
gtaskqueue_terminate(struct thread **pp, struct gtaskqueue *tq)
{

	while (tq->tq_tcount > 0 || tq->tq_callouts > 0) {
		wakeup(tq);
		TQ_SLEEP(tq, pp, &tq->tq_mutex, PWAIT, "taskqueue_destroy", 0);
	}
}

static void
gtaskqueue_free(struct gtaskqueue *queue)
{

	TQ_LOCK(queue);
	queue->tq_flags &= ~TQ_FLAGS_ACTIVE;
	gtaskqueue_terminate(queue->tq_threads, queue);
	KASSERT(TAILQ_EMPTY(&queue->tq_active), ("Tasks still running?"));
	KASSERT(queue->tq_callouts == 0, ("Armed timeout tasks"));
	mtx_destroy(&queue->tq_mutex);
	free(queue->tq_threads, M_GTASKQUEUE);
	free(queue->tq_name, M_GTASKQUEUE);
	free(queue, M_GTASKQUEUE);
}

/*
 * Wait for all to complete, then prevent it from being enqueued
 */
void
grouptask_block(struct grouptask *grouptask)
{
	struct gtaskqueue *queue = grouptask->gt_taskqueue;
	struct gtask *gtask = &grouptask->gt_task;

#ifdef INVARIANTS
	if (queue == NULL) {
		gtask_dump(gtask);
		panic("queue == NULL");
	}
#endif
	TQ_LOCK(queue);
	gtask->ta_flags |= TASK_NOENQUEUE;
  	gtaskqueue_drain_locked(queue, gtask);
	TQ_UNLOCK(queue);
}

void
grouptask_unblock(struct grouptask *grouptask)
{
	struct gtaskqueue *queue = grouptask->gt_taskqueue;
	struct gtask *gtask = &grouptask->gt_task;

#ifdef INVARIANTS
	if (queue == NULL) {
		gtask_dump(gtask);
		panic("queue == NULL");
	}
#endif
	TQ_LOCK(queue);
	gtask->ta_flags &= ~TASK_NOENQUEUE;
	TQ_UNLOCK(queue);
}

int
grouptaskqueue_enqueue(struct gtaskqueue *queue, struct gtask *gtask)
{
#ifdef INVARIANTS
	if (queue == NULL) {
		gtask_dump(gtask);
		panic("queue == NULL");
	}
#endif
	TQ_LOCK(queue);
	if (gtask->ta_flags & TASK_ENQUEUED) {
		TQ_UNLOCK(queue);
		return (0);
	}
	if (gtask->ta_flags & TASK_NOENQUEUE) {
		TQ_UNLOCK(queue);
		return (EAGAIN);
	}
	STAILQ_INSERT_TAIL(&queue->tq_queue, gtask, ta_link);
	gtask->ta_flags |= TASK_ENQUEUED;
	TQ_UNLOCK(queue);
	if ((queue->tq_flags & TQ_FLAGS_BLOCKED) == 0)
		queue->tq_enqueue(queue->tq_context);
	return (0);
}

static void
gtaskqueue_task_nop_fn(void *context)
{
}

/*
 * Block until all currently queued tasks in this taskqueue
 * have begun execution.  Tasks queued during execution of
 * this function are ignored.
 */
static void
gtaskqueue_drain_tq_queue(struct gtaskqueue *queue)
{
	struct gtask t_barrier;

	if (STAILQ_EMPTY(&queue->tq_queue))
		return;

	/*
	 * Enqueue our barrier after all current tasks, but with
	 * the highest priority so that newly queued tasks cannot
	 * pass it.  Because of the high priority, we can not use
	 * taskqueue_enqueue_locked directly (which drops the lock
	 * anyway) so just insert it at tail while we have the
	 * queue lock.
	 */
	GTASK_INIT(&t_barrier, 0, USHRT_MAX, gtaskqueue_task_nop_fn, &t_barrier);
	STAILQ_INSERT_TAIL(&queue->tq_queue, &t_barrier, ta_link);
	t_barrier.ta_flags |= TASK_ENQUEUED;

	/*
	 * Once the barrier has executed, all previously queued tasks
	 * have completed or are currently executing.
	 */
	while (t_barrier.ta_flags & TASK_ENQUEUED)
		TQ_SLEEP(queue, &t_barrier, &queue->tq_mutex, PWAIT, "-", 0);
}

/*
 * Block until all currently executing tasks for this taskqueue
 * complete.  Tasks that begin execution during the execution
 * of this function are ignored.
 */
static void
gtaskqueue_drain_tq_active(struct gtaskqueue *queue)
{
	struct gtaskqueue_busy tb_marker, *tb_first;

	if (TAILQ_EMPTY(&queue->tq_active))
		return;

	/* Block taskq_terminate().*/
	queue->tq_callouts++;

	/*
	 * Wait for all currently executing taskqueue threads
	 * to go idle.
	 */
	tb_marker.tb_running = TB_DRAIN_WAITER;
	TAILQ_INSERT_TAIL(&queue->tq_active, &tb_marker, tb_link);
	while (TAILQ_FIRST(&queue->tq_active) != &tb_marker)
		TQ_SLEEP(queue, &tb_marker, &queue->tq_mutex, PWAIT, "-", 0);
	TAILQ_REMOVE(&queue->tq_active, &tb_marker, tb_link);

	/*
	 * Wakeup any other drain waiter that happened to queue up
	 * without any intervening active thread.
	 */
	tb_first = TAILQ_FIRST(&queue->tq_active);
	if (tb_first != NULL && tb_first->tb_running == TB_DRAIN_WAITER)
		wakeup(tb_first);

	/* Release taskqueue_terminate(). */
	queue->tq_callouts--;
	if ((queue->tq_flags & TQ_FLAGS_ACTIVE) == 0)
		wakeup_one(queue->tq_threads);
}

void
gtaskqueue_block(struct gtaskqueue *queue)
{

	TQ_LOCK(queue);
	queue->tq_flags |= TQ_FLAGS_BLOCKED;
	TQ_UNLOCK(queue);
}

void
gtaskqueue_unblock(struct gtaskqueue *queue)
{

	TQ_LOCK(queue);
	queue->tq_flags &= ~TQ_FLAGS_BLOCKED;
	if (!STAILQ_EMPTY(&queue->tq_queue))
		queue->tq_enqueue(queue->tq_context);
	TQ_UNLOCK(queue);
}

static void
gtaskqueue_run_locked(struct gtaskqueue *queue)
{
	struct gtaskqueue_busy tb;
	struct gtaskqueue_busy *tb_first;
	struct gtask *gtask;

	KASSERT(queue != NULL, ("tq is NULL"));
	TQ_ASSERT_LOCKED(queue);
	tb.tb_running = NULL;

	while (STAILQ_FIRST(&queue->tq_queue)) {
		TAILQ_INSERT_TAIL(&queue->tq_active, &tb, tb_link);

		/*
		 * Carefully remove the first task from the queue and
		 * clear its TASK_ENQUEUED flag
		 */
		gtask = STAILQ_FIRST(&queue->tq_queue);
		KASSERT(gtask != NULL, ("task is NULL"));
		STAILQ_REMOVE_HEAD(&queue->tq_queue, ta_link);
		gtask->ta_flags &= ~TASK_ENQUEUED;
		tb.tb_running = gtask;
		TQ_UNLOCK(queue);

		KASSERT(gtask->ta_func != NULL, ("task->ta_func is NULL"));
		gtask->ta_func(gtask->ta_context);

		TQ_LOCK(queue);
		tb.tb_running = NULL;
		wakeup(gtask);

		TAILQ_REMOVE(&queue->tq_active, &tb, tb_link);
		tb_first = TAILQ_FIRST(&queue->tq_active);
		if (tb_first != NULL &&
		    tb_first->tb_running == TB_DRAIN_WAITER)
			wakeup(tb_first);
	}
}

static int
task_is_running(struct gtaskqueue *queue, struct gtask *gtask)
{
	struct gtaskqueue_busy *tb;

	TQ_ASSERT_LOCKED(queue);
	TAILQ_FOREACH(tb, &queue->tq_active, tb_link) {
		if (tb->tb_running == gtask)
			return (1);
	}
	return (0);
}

static int
gtaskqueue_cancel_locked(struct gtaskqueue *queue, struct gtask *gtask)
{

	if (gtask->ta_flags & TASK_ENQUEUED)
		STAILQ_REMOVE(&queue->tq_queue, gtask, gtask, ta_link);
	gtask->ta_flags &= ~TASK_ENQUEUED;
	return (task_is_running(queue, gtask) ? EBUSY : 0);
}

int
gtaskqueue_cancel(struct gtaskqueue *queue, struct gtask *gtask)
{
	int error;

	TQ_LOCK(queue);
	error = gtaskqueue_cancel_locked(queue, gtask);
	TQ_UNLOCK(queue);

	return (error);
}

static void
gtaskqueue_drain_locked(struct gtaskqueue *queue, struct gtask *gtask)
{
	while ((gtask->ta_flags & TASK_ENQUEUED) || task_is_running(queue, gtask))
		TQ_SLEEP(queue, gtask, &queue->tq_mutex, PWAIT, "-", 0);
}

void
gtaskqueue_drain(struct gtaskqueue *queue, struct gtask *gtask)
{

	if (!queue->tq_spin)
		WITNESS_WARN(WARN_GIANTOK | WARN_SLEEPOK, NULL, __func__);

	TQ_LOCK(queue);
	gtaskqueue_drain_locked(queue, gtask);
	TQ_UNLOCK(queue);
}

void
gtaskqueue_drain_all(struct gtaskqueue *queue)
{

	if (!queue->tq_spin)
		WITNESS_WARN(WARN_GIANTOK | WARN_SLEEPOK, NULL, __func__);

	TQ_LOCK(queue);
	gtaskqueue_drain_tq_queue(queue);
	gtaskqueue_drain_tq_active(queue);
	TQ_UNLOCK(queue);
}

static int
_gtaskqueue_start_threads(struct gtaskqueue **tqp, int count, int pri,
    cpuset_t *mask, const char *name, va_list ap)
{
	char ktname[MAXCOMLEN + 1];
	struct thread *td;
	struct gtaskqueue *tq;
	int i, error;

	if (count <= 0)
		return (EINVAL);

	vsnprintf(ktname, sizeof(ktname), name, ap);
	tq = *tqp;

	tq->tq_threads = malloc(sizeof(struct thread *) * count, M_GTASKQUEUE,
	    M_NOWAIT | M_ZERO);
	if (tq->tq_threads == NULL) {
		printf("%s: no memory for %s threads\n", __func__, ktname);
		return (ENOMEM);
	}

	for (i = 0; i < count; i++) {
		if (count == 1)
			error = kthread_add(gtaskqueue_thread_loop, tqp, NULL,
			    &tq->tq_threads[i], RFSTOPPED, 0, "%s", ktname);
		else
			error = kthread_add(gtaskqueue_thread_loop, tqp, NULL,
			    &tq->tq_threads[i], RFSTOPPED, 0,
			    "%s_%d", ktname, i);
		if (error) {
			/* should be ok to continue, taskqueue_free will dtrt */
			printf("%s: kthread_add(%s): error %d", __func__,
			    ktname, error);
			tq->tq_threads[i] = NULL;		/* paranoid */
		} else
			tq->tq_tcount++;
	}
	for (i = 0; i < count; i++) {
		if (tq->tq_threads[i] == NULL)
			continue;
		td = tq->tq_threads[i];
		if (mask) {
			error = cpuset_setthread(td->td_tid, mask);
			/*
			 * Failing to pin is rarely an actual fatal error;
			 * it'll just affect performance.
			 */
			if (error)
				printf("%s: curthread=%llu: can't pin; "
				    "error=%d\n",
				    __func__,
				    (unsigned long long) td->td_tid,
				    error);
		}
		thread_lock(td);
		sched_prio(td, pri);
		sched_add(td, SRQ_BORING);
		thread_unlock(td);
	}

	return (0);
}

static int
gtaskqueue_start_threads(struct gtaskqueue **tqp, int count, int pri,
    const char *name, ...)
{
	va_list ap;
	int error;

	va_start(ap, name);
	error = _gtaskqueue_start_threads(tqp, count, pri, NULL, name, ap);
	va_end(ap);
	return (error);
}

static inline void
gtaskqueue_run_callback(struct gtaskqueue *tq,
    enum taskqueue_callback_type cb_type)
{
	taskqueue_callback_fn tq_callback;

	TQ_ASSERT_UNLOCKED(tq);
	tq_callback = tq->tq_callbacks[cb_type];
	if (tq_callback != NULL)
		tq_callback(tq->tq_cb_contexts[cb_type]);
}

static void
gtaskqueue_thread_loop(void *arg)
{
	struct gtaskqueue **tqp, *tq;

	tqp = arg;
	tq = *tqp;
	gtaskqueue_run_callback(tq, TASKQUEUE_CALLBACK_TYPE_INIT);
	TQ_LOCK(tq);
	while ((tq->tq_flags & TQ_FLAGS_ACTIVE) != 0) {
		/* XXX ? */
		gtaskqueue_run_locked(tq);
		/*
		 * Because taskqueue_run() can drop tq_mutex, we need to
		 * check if the TQ_FLAGS_ACTIVE flag wasn't removed in the
		 * meantime, which means we missed a wakeup.
		 */
		if ((tq->tq_flags & TQ_FLAGS_ACTIVE) == 0)
			break;
		TQ_SLEEP(tq, tq, &tq->tq_mutex, 0, "-", 0);
	}
	gtaskqueue_run_locked(tq);
	/*
	 * This thread is on its way out, so just drop the lock temporarily
	 * in order to call the shutdown callback.  This allows the callback
	 * to look at the taskqueue, even just before it dies.
	 */
	TQ_UNLOCK(tq);
	gtaskqueue_run_callback(tq, TASKQUEUE_CALLBACK_TYPE_SHUTDOWN);
	TQ_LOCK(tq);

	/* rendezvous with thread that asked us to terminate */
	tq->tq_tcount--;
	wakeup_one(tq->tq_threads);
	TQ_UNLOCK(tq);
	kthread_exit();
}

static void
gtaskqueue_thread_enqueue(void *context)
{
	struct gtaskqueue **tqp, *tq;

	tqp = context;
	tq = *tqp;
	wakeup_one(tq);
}


static struct gtaskqueue *
gtaskqueue_create_fast(const char *name, int mflags,
		 taskqueue_enqueue_fn enqueue, void *context)
{
	return _gtaskqueue_create(name, mflags, enqueue, context,
			MTX_SPIN, "fast_taskqueue");
}


struct taskqgroup_cpu {
	LIST_HEAD(, grouptask)	tgc_tasks;
	struct gtaskqueue	*tgc_taskq;
	int	tgc_cnt;
	int	tgc_cpu;
};

struct taskqgroup {
	struct taskqgroup_cpu tqg_queue[MAXCPU];
	struct mtx	tqg_lock;
	const char *	tqg_name;
	int		tqg_adjusting;
	int		tqg_stride;
	int		tqg_cnt;
};

struct taskq_bind_task {
	struct gtask bt_task;
	int	bt_cpuid;
};

static void
taskqgroup_cpu_create(struct taskqgroup *qgroup, int idx, int cpu)
{
	struct taskqgroup_cpu *qcpu;

	qcpu = &qgroup->tqg_queue[idx];
	LIST_INIT(&qcpu->tgc_tasks);
	qcpu->tgc_taskq = gtaskqueue_create_fast(NULL, M_WAITOK,
	    taskqueue_thread_enqueue, &qcpu->tgc_taskq);
	gtaskqueue_start_threads(&qcpu->tgc_taskq, 1, PI_SOFT,
	    "%s_%d", qgroup->tqg_name, idx);
	qcpu->tgc_cpu = cpu;
}

static void
taskqgroup_cpu_remove(struct taskqgroup *qgroup, int idx)
{

	gtaskqueue_free(qgroup->tqg_queue[idx].tgc_taskq);
}

/*
 * Find the taskq with least # of tasks that doesn't currently have any
 * other queues from the uniq identifier.
 */
static int
taskqgroup_find(struct taskqgroup *qgroup, void *uniq)
{
	struct grouptask *n;
	int i, idx, mincnt;
	int strict;

	mtx_assert(&qgroup->tqg_lock, MA_OWNED);
	if (qgroup->tqg_cnt == 0)
		return (0);
	idx = -1;
	mincnt = INT_MAX;
	/*
	 * Two passes;  First scan for a queue with the least tasks that
	 * does not already service this uniq id.  If that fails simply find
	 * the queue with the least total tasks;
	 */
	for (strict = 1; mincnt == INT_MAX; strict = 0) {
		for (i = 0; i < qgroup->tqg_cnt; i++) {
			if (qgroup->tqg_queue[i].tgc_cnt > mincnt)
				continue;
			if (strict) {
				LIST_FOREACH(n,
				    &qgroup->tqg_queue[i].tgc_tasks, gt_list)
					if (n->gt_uniq == uniq)
						break;
				if (n != NULL)
					continue;
			}
			mincnt = qgroup->tqg_queue[i].tgc_cnt;
			idx = i;
		}
	}
	if (idx == -1)
		panic("%s: failed to pick a qid.", __func__);

	return (idx);
}

/*
 * smp_started is unusable since it is not set for UP kernels or even for
 * SMP kernels when there is 1 CPU.  This is usually handled by adding a
 * (mp_ncpus == 1) test, but that would be broken here since we need to
 * to synchronize with the SI_SUB_SMP ordering.  Even in the pure SMP case
 * smp_started only gives a fuzzy ordering relative to SI_SUB_SMP.
 *
 * So maintain our own flag.  It must be set after all CPUs are started
 * and before SI_SUB_SMP:SI_ORDER_ANY so that the SYSINIT for delayed
 * adjustment is properly delayed.  SI_ORDER_FOURTH is clearly before
 * SI_ORDER_ANY and unclearly after the CPUs are started.  It would be
 * simpler for adjustment to pass a flag indicating if it is delayed.
 */ 

static int tqg_smp_started;

static void
tqg_record_smp_started(void *arg)
{
	tqg_smp_started = 1;
}

SYSINIT(tqg_record_smp_started, SI_SUB_SMP, SI_ORDER_FOURTH,
	tqg_record_smp_started, NULL);

void
taskqgroup_attach(struct taskqgroup *qgroup, struct grouptask *gtask,
    void *uniq, device_t dev, struct resource *irq, const char *name)
{
	int cpu, qid, error;

	gtask->gt_uniq = uniq;
	snprintf(gtask->gt_name, GROUPTASK_NAMELEN, "%s", name ? name : "grouptask");
	gtask->gt_dev = dev;
	gtask->gt_irq = irq;
	gtask->gt_cpu = -1;
	mtx_lock(&qgroup->tqg_lock);
	qid = taskqgroup_find(qgroup, uniq);
	qgroup->tqg_queue[qid].tgc_cnt++;
	LIST_INSERT_HEAD(&qgroup->tqg_queue[qid].tgc_tasks, gtask, gt_list);
	gtask->gt_taskqueue = qgroup->tqg_queue[qid].tgc_taskq;
	if (dev != NULL && irq != NULL && tqg_smp_started) {
		cpu = qgroup->tqg_queue[qid].tgc_cpu;
		gtask->gt_cpu = cpu;
		mtx_unlock(&qgroup->tqg_lock);
		error = bus_bind_intr(dev, irq, cpu);
		if (error)
			printf("%s: binding interrupt failed for %s: %d\n",
			    __func__, gtask->gt_name, error);
	} else
		mtx_unlock(&qgroup->tqg_lock);
}

static void
taskqgroup_attach_deferred(struct taskqgroup *qgroup, struct grouptask *gtask)
{
	int qid, cpu, error;

	mtx_lock(&qgroup->tqg_lock);
	qid = taskqgroup_find(qgroup, gtask->gt_uniq);
	cpu = qgroup->tqg_queue[qid].tgc_cpu;
	if (gtask->gt_dev != NULL && gtask->gt_irq != NULL) {
		mtx_unlock(&qgroup->tqg_lock);
		error = bus_bind_intr(gtask->gt_dev, gtask->gt_irq, cpu);
		mtx_lock(&qgroup->tqg_lock);
		if (error)
			printf("%s: binding interrupt failed for %s: %d\n",
			    __func__, gtask->gt_name, error);

	}
	qgroup->tqg_queue[qid].tgc_cnt++;
	LIST_INSERT_HEAD(&qgroup->tqg_queue[qid].tgc_tasks, gtask, gt_list);
	MPASS(qgroup->tqg_queue[qid].tgc_taskq != NULL);
	gtask->gt_taskqueue = qgroup->tqg_queue[qid].tgc_taskq;
	mtx_unlock(&qgroup->tqg_lock);
}

int
taskqgroup_attach_cpu(struct taskqgroup *qgroup, struct grouptask *gtask,
    void *uniq, int cpu, device_t dev, struct resource *irq, const char *name)
{
	int i, qid, error;

	qid = -1;
	gtask->gt_uniq = uniq;
	snprintf(gtask->gt_name, GROUPTASK_NAMELEN, "%s", name ? name : "grouptask");
	gtask->gt_dev = dev;
	gtask->gt_irq = irq;
	gtask->gt_cpu = cpu;
	mtx_lock(&qgroup->tqg_lock);
	if (tqg_smp_started) {
		for (i = 0; i < qgroup->tqg_cnt; i++)
			if (qgroup->tqg_queue[i].tgc_cpu == cpu) {
				qid = i;
				break;
			}
		if (qid == -1) {
			mtx_unlock(&qgroup->tqg_lock);
			printf("%s: qid not found for %s cpu=%d\n", __func__, gtask->gt_name, cpu);
			return (EINVAL);
		}
	} else
		qid = 0;
	qgroup->tqg_queue[qid].tgc_cnt++;
	LIST_INSERT_HEAD(&qgroup->tqg_queue[qid].tgc_tasks, gtask, gt_list);
	gtask->gt_taskqueue = qgroup->tqg_queue[qid].tgc_taskq;
	cpu = qgroup->tqg_queue[qid].tgc_cpu;
	mtx_unlock(&qgroup->tqg_lock);

	if (dev != NULL && irq != NULL && tqg_smp_started) {
		error = bus_bind_intr(dev, irq, cpu);
		if (error)
			printf("%s: binding interrupt failed for %s: %d\n",
			    __func__, gtask->gt_name, error);
	}
	return (0);
}

static int
taskqgroup_attach_cpu_deferred(struct taskqgroup *qgroup, struct grouptask *gtask)
{
	device_t dev;
	struct resource *irq;
	int cpu, error, i, qid;

	qid = -1;
	dev = gtask->gt_dev;
	irq = gtask->gt_irq;
	cpu = gtask->gt_cpu;
	MPASS(tqg_smp_started);
	mtx_lock(&qgroup->tqg_lock);
	for (i = 0; i < qgroup->tqg_cnt; i++)
		if (qgroup->tqg_queue[i].tgc_cpu == cpu) {
			qid = i;
			break;
		}
	if (qid == -1) {
		mtx_unlock(&qgroup->tqg_lock);
		printf("%s: qid not found for %s cpu=%d\n", __func__, gtask->gt_name, cpu);
		return (EINVAL);
	}
	qgroup->tqg_queue[qid].tgc_cnt++;
	LIST_INSERT_HEAD(&qgroup->tqg_queue[qid].tgc_tasks, gtask, gt_list);
	MPASS(qgroup->tqg_queue[qid].tgc_taskq != NULL);
	gtask->gt_taskqueue = qgroup->tqg_queue[qid].tgc_taskq;
	mtx_unlock(&qgroup->tqg_lock);

	if (dev != NULL && irq != NULL) {
		error = bus_bind_intr(dev, irq, cpu);
		if (error)
			printf("%s: binding interrupt failed for %s: %d\n",
			    __func__, gtask->gt_name, error);
	}
	return (0);
}

void
taskqgroup_detach(struct taskqgroup *qgroup, struct grouptask *gtask)
{
	int i;

	grouptask_block(gtask);
	mtx_lock(&qgroup->tqg_lock);
	for (i = 0; i < qgroup->tqg_cnt; i++)
		if (qgroup->tqg_queue[i].tgc_taskq == gtask->gt_taskqueue)
			break;
	if (i == qgroup->tqg_cnt)
		panic("%s: task %s not in group", __func__, gtask->gt_name);
	qgroup->tqg_queue[i].tgc_cnt--;
	LIST_REMOVE(gtask, gt_list);
	mtx_unlock(&qgroup->tqg_lock);
	gtask->gt_taskqueue = NULL;
	gtask->gt_task.ta_flags &= ~TASK_NOENQUEUE;
}

static void
taskqgroup_binder(void *ctx)
{
	struct taskq_bind_task *gtask = (struct taskq_bind_task *)ctx;
	cpuset_t mask;
	int error;

	CPU_ZERO(&mask);
	CPU_SET(gtask->bt_cpuid, &mask);
	error = cpuset_setthread(curthread->td_tid, &mask);
	thread_lock(curthread);
	sched_bind(curthread, gtask->bt_cpuid);
	thread_unlock(curthread);

	if (error)
		printf("%s: binding curthread failed: %d\n", __func__, error);
	free(gtask, M_DEVBUF);
}

static void
taskqgroup_bind(struct taskqgroup *qgroup)
{
	struct taskq_bind_task *gtask;
	int i;

	/*
	 * Bind taskqueue threads to specific CPUs, if they have been assigned
	 * one.
	 */
	if (qgroup->tqg_cnt == 1)
		return;

	for (i = 0; i < qgroup->tqg_cnt; i++) {
		gtask = malloc(sizeof (*gtask), M_DEVBUF, M_WAITOK);
		GTASK_INIT(&gtask->bt_task, 0, 0, taskqgroup_binder, gtask);
		gtask->bt_cpuid = qgroup->tqg_queue[i].tgc_cpu;
		grouptaskqueue_enqueue(qgroup->tqg_queue[i].tgc_taskq,
		    &gtask->bt_task);
	}
}

static void
taskqgroup_config_init(void *arg)
{
	struct taskqgroup *qgroup = qgroup_config;
	LIST_HEAD(, grouptask) gtask_head = LIST_HEAD_INITIALIZER(NULL);

	LIST_SWAP(&gtask_head, &qgroup->tqg_queue[0].tgc_tasks,
	    grouptask, gt_list);
	qgroup->tqg_queue[0].tgc_cnt = 0;
	taskqgroup_cpu_create(qgroup, 0, 0);

	qgroup->tqg_cnt = 1;
	qgroup->tqg_stride = 1;
}

SYSINIT(taskqgroup_config_init, SI_SUB_TASKQ, SI_ORDER_SECOND,
	taskqgroup_config_init, NULL);

static int
_taskqgroup_adjust(struct taskqgroup *qgroup, int cnt, int stride)
{
	LIST_HEAD(, grouptask) gtask_head = LIST_HEAD_INITIALIZER(NULL);
	struct grouptask *gtask;
	int i, k, old_cnt, old_cpu, cpu;

	mtx_assert(&qgroup->tqg_lock, MA_OWNED);

	if (cnt < 1 || cnt * stride > mp_ncpus || !tqg_smp_started) {
		printf("%s: failed cnt: %d stride: %d "
		    "mp_ncpus: %d tqg_smp_started: %d\n",
		    __func__, cnt, stride, mp_ncpus, tqg_smp_started);
		return (EINVAL);
	}
	if (qgroup->tqg_adjusting) {
		printf("%s failed: adjusting\n", __func__);
		return (EBUSY);
	}
	qgroup->tqg_adjusting = 1;
	old_cnt = qgroup->tqg_cnt;
	old_cpu = 0;
	if (old_cnt < cnt)
		old_cpu = qgroup->tqg_queue[old_cnt].tgc_cpu;
	mtx_unlock(&qgroup->tqg_lock);
	/*
	 * Set up queue for tasks added before boot.
	 */
	if (old_cnt == 0) {
		LIST_SWAP(&gtask_head, &qgroup->tqg_queue[0].tgc_tasks,
		    grouptask, gt_list);
		qgroup->tqg_queue[0].tgc_cnt = 0;
	}

	/*
	 * If new taskq threads have been added.
	 */
	cpu = old_cpu;
	for (i = old_cnt; i < cnt; i++) {
		taskqgroup_cpu_create(qgroup, i, cpu);

		for (k = 0; k < stride; k++)
			cpu = CPU_NEXT(cpu);
	}
	mtx_lock(&qgroup->tqg_lock);
	qgroup->tqg_cnt = cnt;
	qgroup->tqg_stride = stride;

	/*
	 * Adjust drivers to use new taskqs.
	 */
	for (i = 0; i < old_cnt; i++) {
		while ((gtask = LIST_FIRST(&qgroup->tqg_queue[i].tgc_tasks))) {
			LIST_REMOVE(gtask, gt_list);
			qgroup->tqg_queue[i].tgc_cnt--;
			LIST_INSERT_HEAD(&gtask_head, gtask, gt_list);
		}
	}
	mtx_unlock(&qgroup->tqg_lock);

	while ((gtask = LIST_FIRST(&gtask_head))) {
		LIST_REMOVE(gtask, gt_list);
		if (gtask->gt_cpu == -1)
			taskqgroup_attach_deferred(qgroup, gtask);
		else if (taskqgroup_attach_cpu_deferred(qgroup, gtask))
			taskqgroup_attach_deferred(qgroup, gtask);
	}

#ifdef INVARIANTS
	mtx_lock(&qgroup->tqg_lock);
	for (i = 0; i < qgroup->tqg_cnt; i++) {
		MPASS(qgroup->tqg_queue[i].tgc_taskq != NULL);
		LIST_FOREACH(gtask, &qgroup->tqg_queue[i].tgc_tasks, gt_list)
			MPASS(gtask->gt_taskqueue != NULL);
	}
	mtx_unlock(&qgroup->tqg_lock);
#endif
	/*
	 * If taskq thread count has been reduced.
	 */
	for (i = cnt; i < old_cnt; i++)
		taskqgroup_cpu_remove(qgroup, i);

	taskqgroup_bind(qgroup);

	mtx_lock(&qgroup->tqg_lock);
	qgroup->tqg_adjusting = 0;

	return (0);
}

int
taskqgroup_adjust(struct taskqgroup *qgroup, int cnt, int stride)
{
	int error;

	mtx_lock(&qgroup->tqg_lock);
	error = _taskqgroup_adjust(qgroup, cnt, stride);
	mtx_unlock(&qgroup->tqg_lock);

	return (error);
}

struct taskqgroup *
taskqgroup_create(const char *name)
{
	struct taskqgroup *qgroup;

	qgroup = malloc(sizeof(*qgroup), M_GTASKQUEUE, M_WAITOK | M_ZERO);
	mtx_init(&qgroup->tqg_lock, "taskqgroup", NULL, MTX_DEF);
	qgroup->tqg_name = name;
	LIST_INIT(&qgroup->tqg_queue[0].tgc_tasks);

	return (qgroup);
}

void
taskqgroup_destroy(struct taskqgroup *qgroup)
{

}

void
taskqgroup_config_gtask_init(void *ctx, struct grouptask *gtask, gtask_fn_t *fn,
    const char *name)
{

	GROUPTASK_INIT(gtask, 0, fn, ctx);
	taskqgroup_attach(qgroup_config, gtask, gtask, NULL, NULL, name);
}

void
taskqgroup_config_gtask_deinit(struct grouptask *gtask)
{

	taskqgroup_detach(qgroup_config, gtask);
}
