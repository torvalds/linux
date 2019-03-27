/*-
 * Copyright (c) 2017 Hans Petter Selasky
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice unmodified, this list of conditions, and the following
 *    disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <linux/workqueue.h>
#include <linux/wait.h>
#include <linux/compat.h>
#include <linux/spinlock.h>

#include <sys/kernel.h>

/*
 * Define all work struct states
 */
enum {
	WORK_ST_IDLE,			/* idle - not started */
	WORK_ST_TIMER,			/* timer is being started */
	WORK_ST_TASK,			/* taskqueue is being queued */
	WORK_ST_EXEC,			/* callback is being called */
	WORK_ST_CANCEL,			/* cancel is being requested */
	WORK_ST_MAX,
};

/*
 * Define global workqueues
 */
static struct workqueue_struct *linux_system_short_wq;
static struct workqueue_struct *linux_system_long_wq;

struct workqueue_struct *system_wq;
struct workqueue_struct *system_long_wq;
struct workqueue_struct *system_unbound_wq;
struct workqueue_struct *system_highpri_wq;
struct workqueue_struct *system_power_efficient_wq;

static int linux_default_wq_cpus = 4;

static void linux_delayed_work_timer_fn(void *);

/*
 * This function atomically updates the work state and returns the
 * previous state at the time of update.
 */
static uint8_t
linux_update_state(atomic_t *v, const uint8_t *pstate)
{
	int c, old;

	c = v->counter;

	while ((old = atomic_cmpxchg(v, c, pstate[c])) != c)
		c = old;

	return (c);
}

/*
 * A LinuxKPI task is allowed to free itself inside the callback function
 * and cannot safely be referred after the callback function has
 * completed. This function gives the linux_work_fn() function a hint,
 * that the task is not going away and can have its state checked
 * again. Without this extra hint LinuxKPI tasks cannot be serialized
 * accross multiple worker threads.
 */
static bool
linux_work_exec_unblock(struct work_struct *work)
{
	struct workqueue_struct *wq;
	struct work_exec *exec;
	bool retval = 0;

	wq = work->work_queue;
	if (unlikely(wq == NULL))
		goto done;

	WQ_EXEC_LOCK(wq);
	TAILQ_FOREACH(exec, &wq->exec_head, entry) {
		if (exec->target == work) {
			exec->target = NULL;
			retval = 1;
			break;
		}
	}
	WQ_EXEC_UNLOCK(wq);
done:
	return (retval);
}

static void
linux_delayed_work_enqueue(struct delayed_work *dwork)
{
	struct taskqueue *tq;

	tq = dwork->work.work_queue->taskqueue;
	taskqueue_enqueue(tq, &dwork->work.work_task);
}

/*
 * This function queues the given work structure on the given
 * workqueue. It returns non-zero if the work was successfully
 * [re-]queued. Else the work is already pending for completion.
 */
bool
linux_queue_work_on(int cpu __unused, struct workqueue_struct *wq,
    struct work_struct *work)
{
	static const uint8_t states[WORK_ST_MAX] __aligned(8) = {
		[WORK_ST_IDLE] = WORK_ST_TASK,		/* start queuing task */
		[WORK_ST_TIMER] = WORK_ST_TIMER,	/* NOP */
		[WORK_ST_TASK] = WORK_ST_TASK,		/* NOP */
		[WORK_ST_EXEC] = WORK_ST_TASK,		/* queue task another time */
		[WORK_ST_CANCEL] = WORK_ST_TASK,	/* start queuing task again */
	};

	if (atomic_read(&wq->draining) != 0)
		return (!work_pending(work));

	switch (linux_update_state(&work->state, states)) {
	case WORK_ST_EXEC:
	case WORK_ST_CANCEL:
		if (linux_work_exec_unblock(work) != 0)
			return (1);
		/* FALLTHROUGH */
	case WORK_ST_IDLE:
		work->work_queue = wq;
		taskqueue_enqueue(wq->taskqueue, &work->work_task);
		return (1);
	default:
		return (0);		/* already on a queue */
	}
}

/*
 * This function queues the given work structure on the given
 * workqueue after a given delay in ticks. It returns non-zero if the
 * work was successfully [re-]queued. Else the work is already pending
 * for completion.
 */
bool
linux_queue_delayed_work_on(int cpu, struct workqueue_struct *wq,
    struct delayed_work *dwork, unsigned delay)
{
	static const uint8_t states[WORK_ST_MAX] __aligned(8) = {
		[WORK_ST_IDLE] = WORK_ST_TIMER,		/* start timeout */
		[WORK_ST_TIMER] = WORK_ST_TIMER,	/* NOP */
		[WORK_ST_TASK] = WORK_ST_TASK,		/* NOP */
		[WORK_ST_EXEC] = WORK_ST_TIMER,		/* start timeout */
		[WORK_ST_CANCEL] = WORK_ST_TIMER,	/* start timeout */
	};

	if (atomic_read(&wq->draining) != 0)
		return (!work_pending(&dwork->work));

	switch (linux_update_state(&dwork->work.state, states)) {
	case WORK_ST_EXEC:
	case WORK_ST_CANCEL:
		if (delay == 0 && linux_work_exec_unblock(&dwork->work) != 0) {
			dwork->timer.expires = jiffies;
			return (1);
		}
		/* FALLTHROUGH */
	case WORK_ST_IDLE:
		dwork->work.work_queue = wq;
		dwork->timer.expires = jiffies + delay;

		if (delay == 0) {
			linux_delayed_work_enqueue(dwork);
		} else if (unlikely(cpu != WORK_CPU_UNBOUND)) {
			mtx_lock(&dwork->timer.mtx);
			callout_reset_on(&dwork->timer.callout, delay,
			    &linux_delayed_work_timer_fn, dwork, cpu);
			mtx_unlock(&dwork->timer.mtx);
		} else {
			mtx_lock(&dwork->timer.mtx);
			callout_reset(&dwork->timer.callout, delay,
			    &linux_delayed_work_timer_fn, dwork);
			mtx_unlock(&dwork->timer.mtx);
		}
		return (1);
	default:
		return (0);		/* already on a queue */
	}
}

void
linux_work_fn(void *context, int pending)
{
	static const uint8_t states[WORK_ST_MAX] __aligned(8) = {
		[WORK_ST_IDLE] = WORK_ST_IDLE,		/* NOP */
		[WORK_ST_TIMER] = WORK_ST_EXEC,		/* delayed work w/o timeout */
		[WORK_ST_TASK] = WORK_ST_EXEC,		/* call callback */
		[WORK_ST_EXEC] = WORK_ST_IDLE,		/* complete callback */
		[WORK_ST_CANCEL] = WORK_ST_EXEC,	/* failed to cancel */
	};
	struct work_struct *work;
	struct workqueue_struct *wq;
	struct work_exec exec;
	struct task_struct *task;

	task = current;

	/* setup local variables */
	work = context;
	wq = work->work_queue;

	/* store target pointer */
	exec.target = work;

	/* insert executor into list */
	WQ_EXEC_LOCK(wq);
	TAILQ_INSERT_TAIL(&wq->exec_head, &exec, entry);
	while (1) {
		switch (linux_update_state(&work->state, states)) {
		case WORK_ST_TIMER:
		case WORK_ST_TASK:
		case WORK_ST_CANCEL:
			WQ_EXEC_UNLOCK(wq);

			/* set current work structure */
			task->work = work;

			/* call work function */
			work->func(work);

			/* set current work structure */
			task->work = NULL;

			WQ_EXEC_LOCK(wq);
			/* check if unblocked */
			if (exec.target != work) {
				/* reapply block */
				exec.target = work;
				break;
			}
			/* FALLTHROUGH */
		default:
			goto done;
		}
	}
done:
	/* remove executor from list */
	TAILQ_REMOVE(&wq->exec_head, &exec, entry);
	WQ_EXEC_UNLOCK(wq);
}

void
linux_delayed_work_fn(void *context, int pending)
{
	struct delayed_work *dwork = context;

	/*
	 * Make sure the timer belonging to the delayed work gets
	 * drained before invoking the work function. Else the timer
	 * mutex may still be in use which can lead to use-after-free
	 * situations, because the work function might free the work
	 * structure before returning.
	 */
	callout_drain(&dwork->timer.callout);

	linux_work_fn(&dwork->work, pending);
}

static void
linux_delayed_work_timer_fn(void *arg)
{
	static const uint8_t states[WORK_ST_MAX] __aligned(8) = {
		[WORK_ST_IDLE] = WORK_ST_IDLE,		/* NOP */
		[WORK_ST_TIMER] = WORK_ST_TASK,		/* start queueing task */
		[WORK_ST_TASK] = WORK_ST_TASK,		/* NOP */
		[WORK_ST_EXEC] = WORK_ST_EXEC,		/* NOP */
		[WORK_ST_CANCEL] = WORK_ST_TASK,	/* failed to cancel */
	};
	struct delayed_work *dwork = arg;

	switch (linux_update_state(&dwork->work.state, states)) {
	case WORK_ST_TIMER:
	case WORK_ST_CANCEL:
		linux_delayed_work_enqueue(dwork);
		break;
	default:
		break;
	}
}

/*
 * This function cancels the given work structure in a synchronous
 * fashion. It returns non-zero if the work was successfully
 * cancelled. Else the work was already cancelled.
 */
bool
linux_cancel_work_sync(struct work_struct *work)
{
	static const uint8_t states[WORK_ST_MAX] __aligned(8) = {
		[WORK_ST_IDLE] = WORK_ST_IDLE,		/* NOP */
		[WORK_ST_TIMER] = WORK_ST_TIMER,	/* can't happen */
		[WORK_ST_TASK] = WORK_ST_IDLE,		/* cancel and drain */
		[WORK_ST_EXEC] = WORK_ST_IDLE,		/* too late, drain */
		[WORK_ST_CANCEL] = WORK_ST_IDLE,	/* cancel and drain */
	};
	struct taskqueue *tq;

	WITNESS_WARN(WARN_GIANTOK | WARN_SLEEPOK, NULL,
	    "linux_cancel_work_sync() might sleep");

	switch (linux_update_state(&work->state, states)) {
	case WORK_ST_IDLE:
	case WORK_ST_TIMER:
		return (0);
	case WORK_ST_EXEC:
		tq = work->work_queue->taskqueue;
		if (taskqueue_cancel(tq, &work->work_task, NULL) != 0)
			taskqueue_drain(tq, &work->work_task);
		return (0);
	default:
		tq = work->work_queue->taskqueue;
		if (taskqueue_cancel(tq, &work->work_task, NULL) != 0)
			taskqueue_drain(tq, &work->work_task);
		return (1);
	}
}

/*
 * This function atomically stops the timer and callback. The timer
 * callback will not be called after this function returns. This
 * functions returns true when the timeout was cancelled. Else the
 * timeout was not started or has already been called.
 */
static inline bool
linux_cancel_timer(struct delayed_work *dwork, bool drain)
{
	bool cancelled;

	mtx_lock(&dwork->timer.mtx);
	cancelled = (callout_stop(&dwork->timer.callout) == 1);
	mtx_unlock(&dwork->timer.mtx);

	/* check if we should drain */
	if (drain)
		callout_drain(&dwork->timer.callout);
	return (cancelled);
}

/*
 * This function cancels the given delayed work structure in a
 * non-blocking fashion. It returns non-zero if the work was
 * successfully cancelled. Else the work may still be busy or already
 * cancelled.
 */
bool
linux_cancel_delayed_work(struct delayed_work *dwork)
{
	static const uint8_t states[WORK_ST_MAX] __aligned(8) = {
		[WORK_ST_IDLE] = WORK_ST_IDLE,		/* NOP */
		[WORK_ST_TIMER] = WORK_ST_CANCEL,	/* try to cancel */
		[WORK_ST_TASK] = WORK_ST_CANCEL,	/* try to cancel */
		[WORK_ST_EXEC] = WORK_ST_EXEC,		/* NOP */
		[WORK_ST_CANCEL] = WORK_ST_CANCEL,	/* NOP */
	};
	struct taskqueue *tq;

	switch (linux_update_state(&dwork->work.state, states)) {
	case WORK_ST_TIMER:
	case WORK_ST_CANCEL:
		if (linux_cancel_timer(dwork, 0)) {
			atomic_cmpxchg(&dwork->work.state,
			    WORK_ST_CANCEL, WORK_ST_IDLE);
			return (1);
		}
		/* FALLTHROUGH */
	case WORK_ST_TASK:
		tq = dwork->work.work_queue->taskqueue;
		if (taskqueue_cancel(tq, &dwork->work.work_task, NULL) == 0) {
			atomic_cmpxchg(&dwork->work.state,
			    WORK_ST_CANCEL, WORK_ST_IDLE);
			return (1);
		}
		/* FALLTHROUGH */
	default:
		return (0);
	}
}

/*
 * This function cancels the given work structure in a synchronous
 * fashion. It returns non-zero if the work was successfully
 * cancelled. Else the work was already cancelled.
 */
bool
linux_cancel_delayed_work_sync(struct delayed_work *dwork)
{
	static const uint8_t states[WORK_ST_MAX] __aligned(8) = {
		[WORK_ST_IDLE] = WORK_ST_IDLE,		/* NOP */
		[WORK_ST_TIMER] = WORK_ST_IDLE,		/* cancel and drain */
		[WORK_ST_TASK] = WORK_ST_IDLE,		/* cancel and drain */
		[WORK_ST_EXEC] = WORK_ST_IDLE,		/* too late, drain */
		[WORK_ST_CANCEL] = WORK_ST_IDLE,	/* cancel and drain */
	};
	struct taskqueue *tq;

	WITNESS_WARN(WARN_GIANTOK | WARN_SLEEPOK, NULL,
	    "linux_cancel_delayed_work_sync() might sleep");

	switch (linux_update_state(&dwork->work.state, states)) {
	case WORK_ST_IDLE:
		return (0);
	case WORK_ST_EXEC:
		tq = dwork->work.work_queue->taskqueue;
		if (taskqueue_cancel(tq, &dwork->work.work_task, NULL) != 0)
			taskqueue_drain(tq, &dwork->work.work_task);
		return (0);
	case WORK_ST_TIMER:
	case WORK_ST_CANCEL:
		if (linux_cancel_timer(dwork, 1)) {
			/*
			 * Make sure taskqueue is also drained before
			 * returning:
			 */
			tq = dwork->work.work_queue->taskqueue;
			taskqueue_drain(tq, &dwork->work.work_task);
			return (1);
		}
		/* FALLTHROUGH */
	default:
		tq = dwork->work.work_queue->taskqueue;
		if (taskqueue_cancel(tq, &dwork->work.work_task, NULL) != 0)
			taskqueue_drain(tq, &dwork->work.work_task);
		return (1);
	}
}

/*
 * This function waits until the given work structure is completed.
 * It returns non-zero if the work was successfully
 * waited for. Else the work was not waited for.
 */
bool
linux_flush_work(struct work_struct *work)
{
	struct taskqueue *tq;
	int retval;

	WITNESS_WARN(WARN_GIANTOK | WARN_SLEEPOK, NULL,
	    "linux_flush_work() might sleep");

	switch (atomic_read(&work->state)) {
	case WORK_ST_IDLE:
		return (0);
	default:
		tq = work->work_queue->taskqueue;
		retval = taskqueue_poll_is_busy(tq, &work->work_task);
		taskqueue_drain(tq, &work->work_task);
		return (retval);
	}
}

/*
 * This function waits until the given delayed work structure is
 * completed. It returns non-zero if the work was successfully waited
 * for. Else the work was not waited for.
 */
bool
linux_flush_delayed_work(struct delayed_work *dwork)
{
	struct taskqueue *tq;
	int retval;

	WITNESS_WARN(WARN_GIANTOK | WARN_SLEEPOK, NULL,
	    "linux_flush_delayed_work() might sleep");

	switch (atomic_read(&dwork->work.state)) {
	case WORK_ST_IDLE:
		return (0);
	case WORK_ST_TIMER:
		if (linux_cancel_timer(dwork, 1))
			linux_delayed_work_enqueue(dwork);
		/* FALLTHROUGH */
	default:
		tq = dwork->work.work_queue->taskqueue;
		retval = taskqueue_poll_is_busy(tq, &dwork->work.work_task);
		taskqueue_drain(tq, &dwork->work.work_task);
		return (retval);
	}
}

/*
 * This function returns true if the given work is pending, and not
 * yet executing:
 */
bool
linux_work_pending(struct work_struct *work)
{
	switch (atomic_read(&work->state)) {
	case WORK_ST_TIMER:
	case WORK_ST_TASK:
	case WORK_ST_CANCEL:
		return (1);
	default:
		return (0);
	}
}

/*
 * This function returns true if the given work is busy.
 */
bool
linux_work_busy(struct work_struct *work)
{
	struct taskqueue *tq;

	switch (atomic_read(&work->state)) {
	case WORK_ST_IDLE:
		return (0);
	case WORK_ST_EXEC:
		tq = work->work_queue->taskqueue;
		return (taskqueue_poll_is_busy(tq, &work->work_task));
	default:
		return (1);
	}
}

struct workqueue_struct *
linux_create_workqueue_common(const char *name, int cpus)
{
	struct workqueue_struct *wq;

	/*
	 * If zero CPUs are specified use the default number of CPUs:
	 */
	if (cpus == 0)
		cpus = linux_default_wq_cpus;

	wq = kmalloc(sizeof(*wq), M_WAITOK | M_ZERO);
	wq->taskqueue = taskqueue_create(name, M_WAITOK,
	    taskqueue_thread_enqueue, &wq->taskqueue);
	atomic_set(&wq->draining, 0);
	taskqueue_start_threads(&wq->taskqueue, cpus, PWAIT, "%s", name);
	TAILQ_INIT(&wq->exec_head);
	mtx_init(&wq->exec_mtx, "linux_wq_exec", NULL, MTX_DEF);

	return (wq);
}

void
linux_destroy_workqueue(struct workqueue_struct *wq)
{
	atomic_inc(&wq->draining);
	drain_workqueue(wq);
	taskqueue_free(wq->taskqueue);
	mtx_destroy(&wq->exec_mtx);
	kfree(wq);
}

void
linux_init_delayed_work(struct delayed_work *dwork, work_func_t func)
{
	memset(dwork, 0, sizeof(*dwork));
	dwork->work.func = func;
	TASK_INIT(&dwork->work.work_task, 0, linux_delayed_work_fn, dwork);
	mtx_init(&dwork->timer.mtx, spin_lock_name("lkpi-dwork"), NULL,
	    MTX_DEF | MTX_NOWITNESS);
	callout_init_mtx(&dwork->timer.callout, &dwork->timer.mtx, 0);
}

struct work_struct *
linux_current_work(void)
{
	return (current->work);
}

static void
linux_work_init(void *arg)
{
	int max_wq_cpus = mp_ncpus + 1;

	/* avoid deadlock when there are too few threads */
	if (max_wq_cpus < 4)
		max_wq_cpus = 4;

	/* set default number of CPUs */
	linux_default_wq_cpus = max_wq_cpus;

	linux_system_short_wq = alloc_workqueue("linuxkpi_short_wq", 0, max_wq_cpus);
	linux_system_long_wq = alloc_workqueue("linuxkpi_long_wq", 0, max_wq_cpus);

	/* populate the workqueue pointers */
	system_long_wq = linux_system_long_wq;
	system_wq = linux_system_short_wq;
	system_power_efficient_wq = linux_system_short_wq;
	system_unbound_wq = linux_system_short_wq;
	system_highpri_wq = linux_system_short_wq;
}
SYSINIT(linux_work_init, SI_SUB_TASKQ, SI_ORDER_THIRD, linux_work_init, NULL);

static void
linux_work_uninit(void *arg)
{
	destroy_workqueue(linux_system_short_wq);
	destroy_workqueue(linux_system_long_wq);

	/* clear workqueue pointers */
	system_long_wq = NULL;
	system_wq = NULL;
	system_power_efficient_wq = NULL;
	system_unbound_wq = NULL;
	system_highpri_wq = NULL;
}
SYSUNINIT(linux_work_uninit, SI_SUB_TASKQ, SI_ORDER_THIRD, linux_work_uninit, NULL);
