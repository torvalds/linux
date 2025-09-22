/*	$OpenBSD: kern_task.c,v 1.36 2025/01/13 03:21:10 mvs Exp $ */

/*
 * Copyright (c) 2013 David Gwynne <dlg@openbsd.org>
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/malloc.h>
#include <sys/mutex.h>
#include <sys/kthread.h>
#include <sys/task.h>
#include <sys/proc.h>
#include <sys/witness.h>

#include "kcov.h"
#if NKCOV > 0
#include <sys/kcov.h>
#endif

#ifdef WITNESS

static struct lock_type taskq_lock_type = {
	.lt_name = "taskq"
};

#define TASKQ_LOCK_FLAGS LO_WITNESS | LO_INITIALIZED | LO_SLEEPABLE | \
    (LO_CLASS_RWLOCK << LO_CLASSSHIFT)

#endif /* WITNESS */

struct taskq_thread {
	SLIST_ENTRY(taskq_thread)
				 tt_entry;
	struct proc		*tt_thread;
};
SLIST_HEAD(taskq_threads, taskq_thread);

struct taskq {
	enum {
		TQ_S_CREATED,
		TQ_S_RUNNING,
		TQ_S_DESTROYED
	}			 tq_state;
	unsigned int		 tq_running;
	unsigned int		 tq_nthreads;
	unsigned int		 tq_flags;
	const char		*tq_name;

	struct mutex		 tq_mtx;
	struct task_list	 tq_worklist;

	struct taskq_threads	 tq_threads;
	unsigned int		 tq_barriers;
	unsigned int		 tq_bgen;
	unsigned int		 tq_bthreads;

#ifdef WITNESS
	struct lock_object	 tq_lock_object;
#endif
};

static const char taskq_sys_name[] = "systq";

struct taskq taskq_sys = {
	.tq_state	= TQ_S_CREATED,
	.tq_running	= 0,
	.tq_nthreads	= 1,
	.tq_flags	= 0,
	.tq_name	= taskq_sys_name,
	.tq_mtx		= MUTEX_INITIALIZER_FLAGS(IPL_HIGH,
			      taskq_sys_name, 0),
	.tq_worklist	= TAILQ_HEAD_INITIALIZER(taskq_sys.tq_worklist),

	.tq_threads	= SLIST_HEAD_INITIALIZER(taskq_sys.tq_threads),
	.tq_barriers	= 0,
	.tq_bgen	= 0,
	.tq_bthreads	= 0,

#ifdef WITNESS
	.tq_lock_object	= {
		.lo_name	= taskq_sys_name,
		.lo_flags	= TASKQ_LOCK_FLAGS,
	},
#endif
};

static const char taskq_sys_mp_name[] = "systqmp";

struct taskq taskq_sys_mp = {
	.tq_state	= TQ_S_CREATED,
	.tq_running	= 0,
	.tq_nthreads	= 1,
	.tq_flags	= TASKQ_MPSAFE,
	.tq_name	= taskq_sys_mp_name,
	.tq_mtx		= MUTEX_INITIALIZER_FLAGS(IPL_HIGH,
			      taskq_sys_mp_name, 0),
	.tq_worklist	= TAILQ_HEAD_INITIALIZER(taskq_sys_mp.tq_worklist),

	.tq_threads	= SLIST_HEAD_INITIALIZER(taskq_sys_mp.tq_threads),
	.tq_barriers	= 0,
	.tq_bgen	= 0,
	.tq_bthreads	= 0,

#ifdef WITNESS
	.tq_lock_object = {
		.lo_name	= taskq_sys_mp_name,
		.lo_flags	= TASKQ_LOCK_FLAGS,
	},
#endif
};

struct taskq *const systq = &taskq_sys;
struct taskq *const systqmp = &taskq_sys_mp;

void	taskq_init(void); /* called in init_main.c */
void	taskq_create_thread(void *);
void	taskq_barrier_task(void *);
int	taskq_next_work(struct taskq *, struct task *);
void	taskq_thread(void *);

void
taskq_init(void)
{
	WITNESS_INIT(&systq->tq_lock_object, &taskq_lock_type);
	kthread_create_deferred(taskq_create_thread, systq);
	WITNESS_INIT(&systqmp->tq_lock_object, &taskq_lock_type);
	kthread_create_deferred(taskq_create_thread, systqmp);
}

struct taskq *
taskq_create(const char *name, unsigned int nthreads, int ipl,
    unsigned int flags)
{
	struct taskq *tq;

	tq = malloc(sizeof(*tq), M_DEVBUF, M_WAITOK);
	if (tq == NULL)
		return (NULL);

	tq->tq_state = TQ_S_CREATED;
	tq->tq_running = 0;
	tq->tq_nthreads = nthreads;
	tq->tq_name = name;
	tq->tq_flags = flags;

	mtx_init_flags(&tq->tq_mtx, ipl, name, 0);
	TAILQ_INIT(&tq->tq_worklist);

	SLIST_INIT(&tq->tq_threads);
	tq->tq_barriers = 0;
	tq->tq_bgen = 0;
	tq->tq_bthreads = 0;

#ifdef WITNESS
	memset(&tq->tq_lock_object, 0, sizeof(tq->tq_lock_object));
	tq->tq_lock_object.lo_name = name;
	tq->tq_lock_object.lo_flags = TASKQ_LOCK_FLAGS;
	witness_init(&tq->tq_lock_object, &taskq_lock_type);
#endif

	/* try to create a thread to guarantee that tasks will be serviced */
	kthread_create_deferred(taskq_create_thread, tq);

	return (tq);
}

void
taskq_destroy(struct taskq *tq)
{
	mtx_enter(&tq->tq_mtx);
	switch (tq->tq_state) {
	case TQ_S_CREATED:
		/* tq is still referenced by taskq_create_thread */
		tq->tq_state = TQ_S_DESTROYED;
		mtx_leave(&tq->tq_mtx);
		return;

	case TQ_S_RUNNING:
		tq->tq_state = TQ_S_DESTROYED;
		break;

	default:
		panic("unexpected %s tq state %u", tq->tq_name, tq->tq_state);
	}

	while (tq->tq_running > 0) {
		wakeup(tq);
		msleep_nsec(&tq->tq_running, &tq->tq_mtx, PWAIT, "tqdestroy",
		    INFSLP);
	}
	mtx_leave(&tq->tq_mtx);

	free(tq, M_DEVBUF, sizeof(*tq));
}

void
taskq_create_thread(void *arg)
{
	struct taskq *tq = arg;
	int rv;

	mtx_enter(&tq->tq_mtx);

	switch (tq->tq_state) {
	case TQ_S_DESTROYED:
		mtx_leave(&tq->tq_mtx);
		free(tq, M_DEVBUF, sizeof(*tq));
		return;

	case TQ_S_CREATED:
		tq->tq_state = TQ_S_RUNNING;
		break;

	default:
		panic("unexpected %s tq state %d", tq->tq_name, tq->tq_state);
	}

	do {
		tq->tq_running++;
		mtx_leave(&tq->tq_mtx);

		rv = kthread_create(taskq_thread, tq, NULL, tq->tq_name);

		mtx_enter(&tq->tq_mtx);
		if (rv != 0) {
			printf("unable to create thread for \"%s\" taskq\n",
			    tq->tq_name);

			tq->tq_running--;
			/* could have been destroyed during kthread_create */
			if (tq->tq_state == TQ_S_DESTROYED &&
			    tq->tq_running == 0)
				wakeup_one(&tq->tq_running);
			break;
		}
	} while (tq->tq_running < tq->tq_nthreads);

	mtx_leave(&tq->tq_mtx);
}

void
taskq_barrier_task(void *p)
{
	struct taskq *tq = p;
	unsigned int gen;

	mtx_enter(&tq->tq_mtx);
	tq->tq_bthreads++;
	wakeup(&tq->tq_bthreads);

	gen = tq->tq_bgen;
	do {
		msleep_nsec(&tq->tq_bgen, &tq->tq_mtx,
		    PWAIT, "tqbarend", INFSLP);
	} while (gen == tq->tq_bgen);
	mtx_leave(&tq->tq_mtx);
}

static void
taskq_do_barrier(struct taskq *tq)
{
	struct task t = TASK_INITIALIZER(taskq_barrier_task, tq);
	struct proc *thread = curproc;
	struct taskq_thread *tt;

	mtx_enter(&tq->tq_mtx);
	tq->tq_barriers++;

	/* is the barrier being run from a task inside the taskq? */
	SLIST_FOREACH(tt, &tq->tq_threads, tt_entry) {
		if (tt->tt_thread == thread) {
			tq->tq_bthreads++;
			wakeup(&tq->tq_bthreads);
			break;
		}
	}

	while (tq->tq_bthreads < tq->tq_nthreads) {
		/* shove the task into the queue for a worker to pick up */
		SET(t.t_flags, TASK_ONQUEUE);
		TAILQ_INSERT_TAIL(&tq->tq_worklist, &t, t_entry);
		wakeup_one(tq);

		msleep_nsec(&tq->tq_bthreads, &tq->tq_mtx,
		    PWAIT, "tqbar", INFSLP);

		/*
		 * another thread running a barrier might have
		 * done this work for us.
		 */
		if (ISSET(t.t_flags, TASK_ONQUEUE))
			TAILQ_REMOVE(&tq->tq_worklist, &t, t_entry);
	}

	if (--tq->tq_barriers == 0) {
		/* we're the last one out */
		tq->tq_bgen++;
		wakeup(&tq->tq_bgen);
		tq->tq_bthreads = 0;
	} else {
		unsigned int gen = tq->tq_bgen;
		do {
			msleep_nsec(&tq->tq_bgen, &tq->tq_mtx,
			    PWAIT, "tqbarwait", INFSLP);
		} while (gen == tq->tq_bgen);
	}
	mtx_leave(&tq->tq_mtx);
}

void
taskq_barrier(struct taskq *tq)
{
	WITNESS_CHECKORDER(&tq->tq_lock_object, LOP_NEWORDER, NULL);

	taskq_do_barrier(tq);
}

void
taskq_del_barrier(struct taskq *tq, struct task *t)
{
	WITNESS_CHECKORDER(&tq->tq_lock_object, LOP_NEWORDER, NULL);

	task_del(tq, t);
	taskq_do_barrier(tq);
}

void
task_set(struct task *t, void (*fn)(void *), void *arg)
{
	t->t_func = fn;
	t->t_arg = arg;
	t->t_flags = 0;
}

int
task_add(struct taskq *tq, struct task *w)
{
	int rv = 0;

	if (ISSET(w->t_flags, TASK_ONQUEUE))
		return (0);

	mtx_enter(&tq->tq_mtx);
	if (!ISSET(w->t_flags, TASK_ONQUEUE)) {
		rv = 1;
		SET(w->t_flags, TASK_ONQUEUE);
		TAILQ_INSERT_TAIL(&tq->tq_worklist, w, t_entry);
#if NKCOV > 0
		if (!kcov_cold)
			w->t_process = curproc->p_p;
#endif
	}
	mtx_leave(&tq->tq_mtx);

	if (rv)
		wakeup_one(tq);

	return (rv);
}

int
task_del(struct taskq *tq, struct task *w)
{
	int rv = 0;

	if (!ISSET(w->t_flags, TASK_ONQUEUE))
		return (0);

	mtx_enter(&tq->tq_mtx);
	if (ISSET(w->t_flags, TASK_ONQUEUE)) {
		rv = 1;
		CLR(w->t_flags, TASK_ONQUEUE);
		TAILQ_REMOVE(&tq->tq_worklist, w, t_entry);
	}
	mtx_leave(&tq->tq_mtx);

	return (rv);
}

int
taskq_next_work(struct taskq *tq, struct task *work)
{
	struct task *next;

	mtx_enter(&tq->tq_mtx);
	while ((next = TAILQ_FIRST(&tq->tq_worklist)) == NULL) {
		if (tq->tq_state != TQ_S_RUNNING) {
			mtx_leave(&tq->tq_mtx);
			return (0);
		}

		msleep_nsec(tq, &tq->tq_mtx, PWAIT, "bored", INFSLP);
	}

	TAILQ_REMOVE(&tq->tq_worklist, next, t_entry);
	CLR(next->t_flags, TASK_ONQUEUE);

	*work = *next; /* copy to caller to avoid races */

	next = TAILQ_FIRST(&tq->tq_worklist);
	mtx_leave(&tq->tq_mtx);

	if (next != NULL && tq->tq_nthreads > 1)
		wakeup_one(tq);

	return (1);
}

void
taskq_thread(void *xtq)
{
	struct taskq_thread self = { .tt_thread = curproc };
	struct taskq *tq = xtq;
	struct task work;
	int last;

	if (ISSET(tq->tq_flags, TASKQ_MPSAFE))
		KERNEL_UNLOCK();

	mtx_enter(&tq->tq_mtx);
	SLIST_INSERT_HEAD(&tq->tq_threads, &self, tt_entry);
	mtx_leave(&tq->tq_mtx);

	WITNESS_CHECKORDER(&tq->tq_lock_object, LOP_NEWORDER, NULL);

	while (taskq_next_work(tq, &work)) {
		WITNESS_LOCK(&tq->tq_lock_object, 0);
#if NKCOV > 0
		kcov_remote_enter(KCOV_REMOTE_COMMON, work.t_process);
#endif
		(*work.t_func)(work.t_arg);
#if NKCOV > 0
		kcov_remote_leave(KCOV_REMOTE_COMMON, work.t_process);
#endif
		WITNESS_UNLOCK(&tq->tq_lock_object, 0);
		sched_pause(yield);
	}

	mtx_enter(&tq->tq_mtx);
	SLIST_REMOVE(&tq->tq_threads, &self, taskq_thread, tt_entry);
	last = (--tq->tq_running == 0);
	mtx_leave(&tq->tq_mtx);

	if (ISSET(tq->tq_flags, TASKQ_MPSAFE))
		KERNEL_LOCK();

	if (last)
		wakeup_one(&tq->tq_running);

	kthread_exit(0);
}
