/*	$OpenBSD: rthread_sync.c,v 1.6 2024/01/10 04:28:43 cheloha Exp $ */
/*
 * Copyright (c) 2004,2005 Ted Unangst <tedu@openbsd.org>
 * Copyright (c) 2012 Philip Guenther <guenther@openbsd.org>
 * All Rights Reserved.
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
/*
 * Mutexes and conditions - synchronization functions.
 */

#include <assert.h>
#include <errno.h>
#include <pthread.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "rthread.h"
#include "cancel.h"		/* in libc/include */

static _atomic_lock_t static_init_lock = _SPINLOCK_UNLOCKED;

/*
 * mutexen
 */
int
pthread_mutex_init(pthread_mutex_t *mutexp, const pthread_mutexattr_t *attr)
{
	struct pthread_mutex *mutex;

	mutex = calloc(1, sizeof(*mutex));
	if (!mutex)
		return (errno);
	mutex->lock = _SPINLOCK_UNLOCKED;
	TAILQ_INIT(&mutex->lockers);
	if (attr == NULL) {
		mutex->type = PTHREAD_MUTEX_DEFAULT;
		mutex->prioceiling = -1;
	} else {
		mutex->type = (*attr)->ma_type;
		mutex->prioceiling = (*attr)->ma_protocol ==
		    PTHREAD_PRIO_PROTECT ? (*attr)->ma_prioceiling : -1;
	}
	*mutexp = mutex;

	return (0);
}
DEF_STRONG(pthread_mutex_init);

int
pthread_mutex_destroy(pthread_mutex_t *mutexp)
{
	struct pthread_mutex *mutex;

	assert(mutexp);
	mutex = (struct pthread_mutex *)*mutexp;
	if (mutex) {
		if (mutex->count || mutex->owner != NULL ||
		    !TAILQ_EMPTY(&mutex->lockers)) {
#define MSG "pthread_mutex_destroy on mutex with waiters!\n"
			write(2, MSG, sizeof(MSG) - 1);
#undef MSG
			return (EBUSY);
		}
		free(mutex);
		*mutexp = NULL;
	}
	return (0);
}
DEF_STRONG(pthread_mutex_destroy);

static int
_rthread_mutex_lock(pthread_mutex_t *mutexp, int trywait,
    const struct timespec *abstime)
{
	struct pthread_mutex *mutex;
	pthread_t self = pthread_self();
	int ret = 0;

	/*
	 * If the mutex is statically initialized, perform the dynamic
	 * initialization. Note: _thread_mutex_lock() in libc requires
	 * _rthread_mutex_lock() to perform the mutex init when *mutexp
	 * is NULL.
	 */
	if (*mutexp == NULL) {
		_spinlock(&static_init_lock);
		if (*mutexp == NULL)
			ret = pthread_mutex_init(mutexp, NULL);
		_spinunlock(&static_init_lock);
		if (ret != 0)
			return (EINVAL);
	}
	mutex = (struct pthread_mutex *)*mutexp;

	_rthread_debug(5, "%p: mutex_lock %p\n", (void *)self, (void *)mutex);
	_spinlock(&mutex->lock);
	if (mutex->owner == NULL && TAILQ_EMPTY(&mutex->lockers)) {
		assert(mutex->count == 0);
		mutex->owner = self;
	} else if (mutex->owner == self) {
		assert(mutex->count > 0);

		/* already owner?  handle recursive behavior */
		if (mutex->type != PTHREAD_MUTEX_RECURSIVE)
		{
			if (trywait ||
			    mutex->type == PTHREAD_MUTEX_ERRORCHECK) {
				_spinunlock(&mutex->lock);
				return (trywait ? EBUSY : EDEADLK);
			}

			/* self-deadlock is disallowed by strict */
			if (mutex->type == PTHREAD_MUTEX_STRICT_NP &&
			    abstime == NULL)
				abort();

			/* self-deadlock, possibly until timeout */
			while (__thrsleep(self, CLOCK_REALTIME, abstime,
			    &mutex->lock, NULL) != EWOULDBLOCK)
				_spinlock(&mutex->lock);
			return (ETIMEDOUT);
		}
		if (mutex->count == INT_MAX) {
			_spinunlock(&mutex->lock);
			return (EAGAIN);
		}
	} else if (trywait) {
		/* try failed */
		_spinunlock(&mutex->lock);
		return (EBUSY);
	} else {
		/* add to the wait queue and block until at the head */
		TAILQ_INSERT_TAIL(&mutex->lockers, self, waiting);
		while (mutex->owner != self) {
			ret = __thrsleep(self, CLOCK_REALTIME, abstime,
			    &mutex->lock, NULL);
			_spinlock(&mutex->lock);
			assert(mutex->owner != NULL);
			if (ret == EWOULDBLOCK) {
				if (mutex->owner == self)
					break;
				TAILQ_REMOVE(&mutex->lockers, self, waiting);
				_spinunlock(&mutex->lock);
				return (ETIMEDOUT);
			}
		}
	}

	mutex->count++;
	_spinunlock(&mutex->lock);

	return (0);
}

int
pthread_mutex_lock(pthread_mutex_t *p)
{
	return (_rthread_mutex_lock(p, 0, NULL));
}
DEF_STRONG(pthread_mutex_lock);

int
pthread_mutex_trylock(pthread_mutex_t *p)
{
	return (_rthread_mutex_lock(p, 1, NULL));
}

int
pthread_mutex_timedlock(pthread_mutex_t *p, const struct timespec *abstime)
{
	return (_rthread_mutex_lock(p, 0, abstime));
}

int
pthread_mutex_unlock(pthread_mutex_t *mutexp)
{
	pthread_t self = pthread_self();
	struct pthread_mutex *mutex = (struct pthread_mutex *)*mutexp;

	_rthread_debug(5, "%p: mutex_unlock %p\n", (void *)self,
	    (void *)mutex);

	if (mutex == NULL)
#if PTHREAD_MUTEX_DEFAULT == PTHREAD_MUTEX_ERRORCHECK
		return (EPERM);
#elif PTHREAD_MUTEX_DEFAULT == PTHREAD_MUTEX_NORMAL
		return(0);
#else
		abort();
#endif

	if (mutex->owner != self) {
		if (mutex->type == PTHREAD_MUTEX_ERRORCHECK ||
		    mutex->type == PTHREAD_MUTEX_RECURSIVE)
			return (EPERM);
		else {
			/*
			 * For mutex type NORMAL our undefined behavior for
			 * unlocking an unlocked mutex is to succeed without
			 * error.  All other undefined behaviors are to
			 * abort() immediately.
			 */
			if (mutex->owner == NULL &&
			    mutex->type == PTHREAD_MUTEX_NORMAL)
				return (0);
			else
				abort();
		}
	}

	if (--mutex->count == 0) {
		pthread_t next;

		_spinlock(&mutex->lock);
		mutex->owner = next = TAILQ_FIRST(&mutex->lockers);
		if (next != NULL)
			TAILQ_REMOVE(&mutex->lockers, next, waiting);
		_spinunlock(&mutex->lock);
		if (next != NULL)
			__thrwakeup(next, 1);
	}

	return (0);
}
DEF_STRONG(pthread_mutex_unlock);

/*
 * condition variables
 */
int
pthread_cond_init(pthread_cond_t *condp, const pthread_condattr_t *attr)
{
	pthread_cond_t cond;

	cond = calloc(1, sizeof(*cond));
	if (!cond)
		return (errno);
	cond->lock = _SPINLOCK_UNLOCKED;
	TAILQ_INIT(&cond->waiters);
	if (attr == NULL)
		cond->clock = CLOCK_REALTIME;
	else
		cond->clock = (*attr)->ca_clock;
	*condp = cond;

	return (0);
}
DEF_STRONG(pthread_cond_init);

int
pthread_cond_destroy(pthread_cond_t *condp)
{
	pthread_cond_t cond;

	assert(condp);
	cond = *condp;
	if (cond) {
		if (!TAILQ_EMPTY(&cond->waiters)) {
#define MSG "pthread_cond_destroy on condvar with waiters!\n"
			write(2, MSG, sizeof(MSG) - 1);
#undef MSG
			return (EBUSY);
		}
		free(cond);
	}
	*condp = NULL;

	return (0);
}

int
pthread_cond_timedwait(pthread_cond_t *condp, pthread_mutex_t *mutexp,
    const struct timespec *abstime)
{
	pthread_cond_t cond;
	struct pthread_mutex *mutex = (struct pthread_mutex *)*mutexp;
	struct tib *tib = TIB_GET();
	pthread_t self = tib->tib_thread;
	pthread_t next;
	int mutex_count;
	int canceled = 0;
	int rv = 0;
	int error;
	PREP_CANCEL_POINT(tib);

	if (!*condp)
		if ((error = pthread_cond_init(condp, NULL)))
			return (error);
	cond = *condp;
	_rthread_debug(5, "%p: cond_timed %p,%p\n", (void *)self,
	    (void *)cond, (void *)mutex);

	if (mutex == NULL)
#if PTHREAD_MUTEX_DEFAULT == PTHREAD_MUTEX_ERRORCHECK
		return (EPERM);
#else
		abort();
#endif

	if (mutex->owner != self) {
		if (mutex->type == PTHREAD_MUTEX_ERRORCHECK)
			return (EPERM);
		else
			abort();
	}

	if (abstime == NULL || abstime->tv_nsec < 0 ||
	    abstime->tv_nsec >= 1000000000)
		return (EINVAL);

	ENTER_DELAYED_CANCEL_POINT(tib, self);

	_spinlock(&cond->lock);

	/* mark the condvar as being associated with this mutex */
	if (cond->mutex == NULL) {
		cond->mutex = mutex;
		assert(TAILQ_EMPTY(&cond->waiters));
	} else if (cond->mutex != mutex) {
		assert(cond->mutex == mutex);
		_spinunlock(&cond->lock);
		LEAVE_CANCEL_POINT_INNER(tib, 1);
		return (EINVAL);
	} else
		assert(! TAILQ_EMPTY(&cond->waiters));

	/* snag the count in case this is a recursive mutex */
	mutex_count = mutex->count;

	/* transfer from the mutex queue to the condvar queue */
	_spinlock(&mutex->lock);
	self->blocking_cond = cond;
	TAILQ_INSERT_TAIL(&cond->waiters, self, waiting);
	_spinunlock(&cond->lock);

	/* wake the next guy blocked on the mutex */
	mutex->count = 0;
	mutex->owner = next = TAILQ_FIRST(&mutex->lockers);
	if (next != NULL) {
		TAILQ_REMOVE(&mutex->lockers, next, waiting);
		__thrwakeup(next, 1);
	}

	/* wait until we're the owner of the mutex again */
	while (mutex->owner != self) {
		error = __thrsleep(self, cond->clock, abstime,
		    &mutex->lock, &self->delayed_cancel);

		/*
		 * If abstime == NULL, then we're definitely waiting
		 * on the mutex instead of the condvar, and are
		 * just waiting for mutex ownership, regardless of
		 * why we woke up.
		 */
		if (abstime == NULL) {
			_spinlock(&mutex->lock);
			continue;
		}

		/*
		 * If we took a normal signal (not from
		 * cancellation) then we should just go back to
		 * sleep without changing state (timeouts, etc).
		 */
		if ((error == EINTR || error == ECANCELED) &&
		    (tib->tib_canceled == 0 ||
		    (tib->tib_cantcancel & CANCEL_DISABLED))) {
			_spinlock(&mutex->lock);
			continue;
		}

		/*
		 * The remaining reasons for waking up (normal
		 * wakeup, timeout, and cancellation) all mean that
		 * we won't be staying in the condvar queue and
		 * we'll no longer time out or be cancelable.
		 */
		abstime = NULL;
		LEAVE_CANCEL_POINT_INNER(tib, 0);

		/*
		 * If we're no longer in the condvar's queue then
		 * we're just waiting for mutex ownership.  Need
		 * cond->lock here to prevent race with cond_signal().
		 */
		_spinlock(&cond->lock);
		if (self->blocking_cond == NULL) {
			_spinunlock(&cond->lock);
			_spinlock(&mutex->lock);
			continue;
		}
		assert(self->blocking_cond == cond);

		/* if timeout or canceled, make note of that */
		if (error == EWOULDBLOCK)
			rv = ETIMEDOUT;
		else if (error == EINTR)
			canceled = 1;

		/* transfer between the queues */
		TAILQ_REMOVE(&cond->waiters, self, waiting);
		assert(mutex == cond->mutex);
		if (TAILQ_EMPTY(&cond->waiters))
			cond->mutex = NULL;
		self->blocking_cond = NULL;
		_spinunlock(&cond->lock);
		_spinlock(&mutex->lock);

		/* mutex unlocked right now? */
		if (mutex->owner == NULL &&
		    TAILQ_EMPTY(&mutex->lockers)) {
			assert(mutex->count == 0);
			mutex->owner = self;
			break;
		}
		TAILQ_INSERT_TAIL(&mutex->lockers, self, waiting);
	}

	/* restore the mutex's count */
	mutex->count = mutex_count;
	_spinunlock(&mutex->lock);

	LEAVE_CANCEL_POINT_INNER(tib, canceled);

	return (rv);
}

int
pthread_cond_wait(pthread_cond_t *condp, pthread_mutex_t *mutexp)
{
	pthread_cond_t cond;
	struct pthread_mutex *mutex = (struct pthread_mutex *)*mutexp;
	struct tib *tib = TIB_GET();
	pthread_t self = tib->tib_thread;
	pthread_t next;
	int mutex_count;
	int canceled = 0;
	int error;
	PREP_CANCEL_POINT(tib);

	if (!*condp)
		if ((error = pthread_cond_init(condp, NULL)))
			return (error);
	cond = *condp;
	_rthread_debug(5, "%p: cond_wait %p,%p\n", (void *)self,
	    (void *)cond, (void *)mutex);

	if (mutex == NULL)
#if PTHREAD_MUTEX_DEFAULT == PTHREAD_MUTEX_ERRORCHECK
		return (EPERM);
#else
		abort();
#endif

	if (mutex->owner != self) {
		if (mutex->type == PTHREAD_MUTEX_ERRORCHECK)
			return (EPERM);
		else
			abort();
	}

	ENTER_DELAYED_CANCEL_POINT(tib, self);

	_spinlock(&cond->lock);

	/* mark the condvar as being associated with this mutex */
	if (cond->mutex == NULL) {
		cond->mutex = mutex;
		assert(TAILQ_EMPTY(&cond->waiters));
	} else if (cond->mutex != mutex) {
		assert(cond->mutex == mutex);
		_spinunlock(&cond->lock);
		LEAVE_CANCEL_POINT_INNER(tib, 1);
		return (EINVAL);
	} else
		assert(! TAILQ_EMPTY(&cond->waiters));

	/* snag the count in case this is a recursive mutex */
	mutex_count = mutex->count;

	/* transfer from the mutex queue to the condvar queue */
	_spinlock(&mutex->lock);
	self->blocking_cond = cond;
	TAILQ_INSERT_TAIL(&cond->waiters, self, waiting);
	_spinunlock(&cond->lock);

	/* wake the next guy blocked on the mutex */
	mutex->count = 0;
	mutex->owner = next = TAILQ_FIRST(&mutex->lockers);
	if (next != NULL) {
		TAILQ_REMOVE(&mutex->lockers, next, waiting);
		__thrwakeup(next, 1);
	}

	/* wait until we're the owner of the mutex again */
	while (mutex->owner != self) {
		error = __thrsleep(self, 0, NULL, &mutex->lock,
		    &self->delayed_cancel);

		/*
		 * If we took a normal signal (not from
		 * cancellation) then we should just go back to
		 * sleep without changing state (timeouts, etc).
		 */
		if ((error == EINTR || error == ECANCELED) &&
		    (tib->tib_canceled == 0 ||
		    (tib->tib_cantcancel & CANCEL_DISABLED))) {
			_spinlock(&mutex->lock);
			continue;
		}

		/*
		 * The remaining reasons for waking up (normal
		 * wakeup and cancellation) all mean that we won't
		 * be staying in the condvar queue and we'll no
		 * longer be cancelable.
		 */
		LEAVE_CANCEL_POINT_INNER(tib, 0);

		/*
		 * If we're no longer in the condvar's queue then
		 * we're just waiting for mutex ownership.  Need
		 * cond->lock here to prevent race with cond_signal().
		 */
		_spinlock(&cond->lock);
		if (self->blocking_cond == NULL) {
			_spinunlock(&cond->lock);
			_spinlock(&mutex->lock);
			continue;
		}
		assert(self->blocking_cond == cond);

		/* if canceled, make note of that */
		if (error == EINTR)
			canceled = 1;

		/* transfer between the queues */
		TAILQ_REMOVE(&cond->waiters, self, waiting);
		assert(mutex == cond->mutex);
		if (TAILQ_EMPTY(&cond->waiters))
			cond->mutex = NULL;
		self->blocking_cond = NULL;
		_spinunlock(&cond->lock);
		_spinlock(&mutex->lock);

		/* mutex unlocked right now? */
		if (mutex->owner == NULL &&
		    TAILQ_EMPTY(&mutex->lockers)) {
			assert(mutex->count == 0);
			mutex->owner = self;
			break;
		}
		TAILQ_INSERT_TAIL(&mutex->lockers, self, waiting);
	}

	/* restore the mutex's count */
	mutex->count = mutex_count;
	_spinunlock(&mutex->lock);

	LEAVE_CANCEL_POINT_INNER(tib, canceled);

	return (0);
}


int
pthread_cond_signal(pthread_cond_t *condp)
{
	pthread_cond_t cond;
	struct pthread_mutex *mutex;
	pthread_t thread;
	int wakeup;

	/* uninitialized?  Then there's obviously no one waiting! */
	if (!*condp)
		return 0;

	cond = *condp;
	_rthread_debug(5, "%p: cond_signal %p,%p\n", (void *)pthread_self(),
	    (void *)cond, (void *)cond->mutex);
	_spinlock(&cond->lock);
	thread = TAILQ_FIRST(&cond->waiters);
	if (thread == NULL) {
		assert(cond->mutex == NULL);
		_spinunlock(&cond->lock);
		return (0);
	}

	assert(thread->blocking_cond == cond);
	TAILQ_REMOVE(&cond->waiters, thread, waiting);
	thread->blocking_cond = NULL;

	mutex = cond->mutex;
	assert(mutex != NULL);
	if (TAILQ_EMPTY(&cond->waiters))
		cond->mutex = NULL;

	/* link locks to prevent race with timedwait */
	_spinlock(&mutex->lock);
	_spinunlock(&cond->lock);

	wakeup = mutex->owner == NULL && TAILQ_EMPTY(&mutex->lockers);
	if (wakeup)
		mutex->owner = thread;
	else
		TAILQ_INSERT_TAIL(&mutex->lockers, thread, waiting);
	_spinunlock(&mutex->lock);
	if (wakeup)
		__thrwakeup(thread, 1);

	return (0);
}

int
pthread_cond_broadcast(pthread_cond_t *condp)
{
	pthread_cond_t cond;
	struct pthread_mutex *mutex;
	pthread_t thread;
	pthread_t p;
	int wakeup;

	/* uninitialized?  Then there's obviously no one waiting! */
	if (!*condp)
		return 0;

	cond = *condp;
	_rthread_debug(5, "%p: cond_broadcast %p,%p\n", (void *)pthread_self(),
	    (void *)cond, (void *)cond->mutex);
	_spinlock(&cond->lock);
	thread = TAILQ_FIRST(&cond->waiters);
	if (thread == NULL) {
		assert(cond->mutex == NULL);
		_spinunlock(&cond->lock);
		return (0);
	}

	mutex = cond->mutex;
	assert(mutex != NULL);

	/* walk the list, clearing the "blocked on condvar" pointer */
	p = thread;
	do
		p->blocking_cond = NULL;
	while ((p = TAILQ_NEXT(p, waiting)) != NULL);

	/*
	 * We want to transfer all the threads from the condvar's list
	 * to the mutex's list.  The TAILQ_* macros don't let us do that
	 * efficiently, so this is direct list surgery.  Pay attention!
	 */

	/* 1) attach the first thread to the end of the mutex's list */
	_spinlock(&mutex->lock);
	wakeup = mutex->owner == NULL && TAILQ_EMPTY(&mutex->lockers);
	thread->waiting.tqe_prev = mutex->lockers.tqh_last;
	*(mutex->lockers.tqh_last) = thread;

	/* 2) fix up the end pointer for the mutex's list */
	mutex->lockers.tqh_last = cond->waiters.tqh_last;

	if (wakeup) {
		TAILQ_REMOVE(&mutex->lockers, thread, waiting);
		mutex->owner = thread;
		_spinunlock(&mutex->lock);
		__thrwakeup(thread, 1);
	} else
		_spinunlock(&mutex->lock);

	/* 3) reset the condvar's list and mutex pointer */
	TAILQ_INIT(&cond->waiters);
	assert(cond->mutex != NULL);
	cond->mutex = NULL;
	_spinunlock(&cond->lock);

	return (0);
}
