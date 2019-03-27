/*-
 * Copyright (c) 2017 Mellanox Technologies, Ltd.
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
 *
 * $FreeBSD$
 */

#include <sys/queue.h>

#include <linux/sched.h>
#include <linux/ww_mutex.h>

struct ww_mutex_thread {
	TAILQ_ENTRY(ww_mutex_thread) entry;
	struct thread *thread;
	struct ww_mutex *lock;
};

static TAILQ_HEAD(, ww_mutex_thread) ww_mutex_head;
static struct mtx ww_mutex_global;

static void
linux_ww_init(void *arg)
{
	TAILQ_INIT(&ww_mutex_head);
	mtx_init(&ww_mutex_global, "lkpi-ww-mtx", NULL, MTX_DEF);
}

SYSINIT(ww_init, SI_SUB_LOCK, SI_ORDER_SECOND, linux_ww_init, NULL);

static void
linux_ww_uninit(void *arg)
{
	mtx_destroy(&ww_mutex_global);
}

SYSUNINIT(ww_uninit, SI_SUB_LOCK, SI_ORDER_SECOND, linux_ww_uninit, NULL);

static inline void
linux_ww_lock(void)
{
	mtx_lock(&ww_mutex_global);
}

static inline void
linux_ww_unlock(void)
{
	mtx_unlock(&ww_mutex_global);
}

/* lock a mutex with deadlock avoidance */
int
linux_ww_mutex_lock_sub(struct ww_mutex *lock, int catch_signal)
{
	struct task_struct *task;
	struct ww_mutex_thread entry;
	struct ww_mutex_thread *other;
	int retval = 0;

	task = current;

	linux_ww_lock();
	if (unlikely(sx_try_xlock(&lock->base.sx) == 0)) {
		entry.thread = curthread;
		entry.lock = lock;
		TAILQ_INSERT_TAIL(&ww_mutex_head, &entry, entry);

		do {
			struct thread *owner = (struct thread *)
			    SX_OWNER(lock->base.sx.sx_lock);

			/* scan for deadlock */
			TAILQ_FOREACH(other, &ww_mutex_head, entry) {
				/* skip own thread */
				if (other == &entry)
					continue;
				/*
				 * If another thread is owning our
				 * lock and is at the same time trying
				 * to acquire a lock this thread owns,
				 * that means deadlock.
				 */
				if (other->thread == owner &&
				    (struct thread *)SX_OWNER(
				    other->lock->base.sx.sx_lock) == curthread) {
					retval = -EDEADLK;
					goto done;
				}
			}
			if (catch_signal) {
				retval = -cv_wait_sig(&lock->condvar, &ww_mutex_global);
				if (retval != 0) {
					linux_schedule_save_interrupt_value(task, retval);
					retval = -EINTR;
					goto done;
				}
			} else {
				cv_wait(&lock->condvar, &ww_mutex_global);
			}
		} while (sx_try_xlock(&lock->base.sx) == 0);
done:
		TAILQ_REMOVE(&ww_mutex_head, &entry, entry);

		/* if the lock is free, wakeup next lock waiter, if any */
		if ((struct thread *)SX_OWNER(lock->base.sx.sx_lock) == NULL)
			cv_signal(&lock->condvar);
	}
	linux_ww_unlock();
	return (retval);
}

void
linux_ww_mutex_unlock_sub(struct ww_mutex *lock)
{
	/* protect ww_mutex ownership change */
	linux_ww_lock();
	sx_xunlock(&lock->base.sx);
	/* wakeup a lock waiter, if any */
	cv_signal(&lock->condvar);
	linux_ww_unlock();
}

int
linux_mutex_lock_interruptible(mutex_t *m)
{
	int error;

	error = -sx_xlock_sig(&m->sx);
	if (error != 0) {
		linux_schedule_save_interrupt_value(current, error);
		error = -EINTR;
	}
	return (error);
}

int
linux_down_write_killable(struct rw_semaphore *rw)
{
	int error;

	error = -sx_xlock_sig(&rw->sx);
	if (error != 0) {
		linux_schedule_save_interrupt_value(current, error);
		error = -EINTR;
	}
	return (error);
}
