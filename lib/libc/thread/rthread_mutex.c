/*	$OpenBSD: rthread_mutex.c,v 1.7 2025/08/03 06:42:31 dlg Exp $ */
/*
 * Copyright (c) 2017 Martin Pieuchot <mpi@openbsd.org>
 * Copyright (c) 2012 Philip Guenther <guenther@openbsd.org>
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

#include <errno.h>
#include <pthread.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "rthread.h"
#include "cancel.h"
#include "synch.h"

/*
 * States defined in "Futexes Are Tricky" 5.2
 */
enum {
	UNLOCKED = 0,
	LOCKED = 1,	/* locked without waiter */
	CONTENDED = 2,	/* threads waiting for this mutex */
};

#define SPIN_COUNT	128
#if defined(__i386__) || defined(__amd64__)
#define SPIN_WAIT()	asm volatile("pause": : : "memory")
#else
#define SPIN_WAIT()	do { } while (0)
#endif

static struct __cmtx static_init_lock = __CMTX_INITIALIZER();

int
pthread_mutex_init(pthread_mutex_t *mutexp, const pthread_mutexattr_t *attr)
{
	pthread_mutex_t mutex;

	mutex = calloc(1, sizeof(*mutex));
	if (mutex == NULL)
		return (ENOMEM);

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
	pthread_mutex_t mutex;

	if (mutexp == NULL || *mutexp == NULL)
		return (EINVAL);

	mutex = *mutexp;
	if (mutex) {
		if (mutex->lock != UNLOCKED) {
#define MSG "pthread_mutex_destroy on mutex with waiters!\n"
			write(2, MSG, sizeof(MSG) - 1);
#undef MSG
			return (EBUSY);
		}
		free((void *)mutex);
		*mutexp = NULL;
	}

	return (0);
}
DEF_STRONG(pthread_mutex_destroy);

static int
_rthread_mutex_trylock(pthread_mutex_t mutex, int trywait,
    const struct timespec *abs)
{
	pthread_t self = pthread_self();

	if (atomic_cas_uint(&mutex->lock, UNLOCKED, LOCKED) == UNLOCKED) {
		membar_enter_after_atomic();
		mutex->owner = self;
		return (0);
	}

	if (mutex->owner == self) {
		int type = mutex->type;

		/* already owner?  handle recursive behavior */
		if (type != PTHREAD_MUTEX_RECURSIVE) {
			if (trywait || type == PTHREAD_MUTEX_ERRORCHECK)
				return (trywait ? EBUSY : EDEADLK);

			/* self-deadlock is disallowed by strict */
			if (type == PTHREAD_MUTEX_STRICT_NP && abs == NULL)
				abort();

			/* self-deadlock, possibly until timeout */
			while (_twait(&mutex->type, type, CLOCK_REALTIME,
			    abs) != ETIMEDOUT)
				;
			return (ETIMEDOUT);
		} else {
			if (mutex->count == INT_MAX)
				return (EAGAIN);
			mutex->count++;
			return (0);
		}
	}

	return (EBUSY);
}

static int
_rthread_mutex_timedlock(pthread_mutex_t *mutexp, int trywait,
    const struct timespec *abs, int timed)
{
	pthread_t self = pthread_self();
	pthread_mutex_t mutex;
	unsigned int i, lock;
	int error = 0;

	if (mutexp == NULL)
		return (EINVAL);

	/*
	 * If the mutex is statically initialized, perform the dynamic
	 * initialization. Note: _thread_mutex_lock() in libc requires
	 * pthread_mutex_lock() to perform the mutex init when *mutexp
	 * is NULL.
	 */
	if (*mutexp == NULL) {
		__cmtx_enter(&static_init_lock);
		if (*mutexp == NULL)
			error = pthread_mutex_init(mutexp, NULL);
		__cmtx_leave(&static_init_lock);
		if (error != 0)
			return (EINVAL);
	}

	mutex = *mutexp;
	_rthread_debug(5, "%p: mutex_%slock %p (%p)\n", self,
	    (timed ? "timed" : (trywait ? "try" : "")), (void *)mutex,
	    (void *)mutex->owner);

	error = _rthread_mutex_trylock(mutex, trywait, abs);
	if (error != EBUSY || trywait)
		return (error);

	/* Try hard to not enter the kernel. */
	for (i = 0; i < SPIN_COUNT; i++) {
		if (mutex->lock == UNLOCKED)
			break;

		SPIN_WAIT();
	}

	lock = atomic_cas_uint(&mutex->lock, UNLOCKED, LOCKED);
	if (lock == UNLOCKED) {
		membar_enter_after_atomic();
		mutex->owner = self;
		return (0);
	}

	if (lock != CONTENDED) {
		/* Indicate that we're waiting on this mutex. */
		lock = atomic_swap_uint(&mutex->lock, CONTENDED);
	}

	while (lock != UNLOCKED) {
		error = _twait(&mutex->lock, CONTENDED, CLOCK_REALTIME, abs);
		if (error == ETIMEDOUT)
			return (error);
		/*
		 * We cannot know if there's another waiter, so in
		 * doubt set the state to CONTENDED.
		 */
		lock = atomic_swap_uint(&mutex->lock, CONTENDED);
	}

	membar_enter_after_atomic();
	mutex->owner = self;
	return (0);
}

int
pthread_mutex_trylock(pthread_mutex_t *mutexp)
{
	return (_rthread_mutex_timedlock(mutexp, 1, NULL, 0));
}

int
pthread_mutex_timedlock(pthread_mutex_t *mutexp, const struct timespec *abs)
{
	return (_rthread_mutex_timedlock(mutexp, 0, abs, 1));
}

int
pthread_mutex_lock(pthread_mutex_t *mutexp)
{
	return (_rthread_mutex_timedlock(mutexp, 0, NULL, 0));
}
DEF_STRONG(pthread_mutex_lock);

int
pthread_mutex_unlock(pthread_mutex_t *mutexp)
{
	pthread_t self = pthread_self();
	pthread_mutex_t mutex;

	if (mutexp == NULL)
		return (EINVAL);

	if (*mutexp == NULL)
#if PTHREAD_MUTEX_DEFAULT == PTHREAD_MUTEX_ERRORCHECK
		return (EPERM);
#elif PTHREAD_MUTEX_DEFAULT == PTHREAD_MUTEX_NORMAL
		return(0);
#else
		abort();
#endif

	mutex = *mutexp;
	_rthread_debug(5, "%p: mutex_unlock %p (%p)\n", self, (void *)mutex,
	    (void *)mutex->owner);

	if (mutex->owner != self) {
	_rthread_debug(5, "%p: different owner %p (%p)\n", self, (void *)mutex,
	    (void *)mutex->owner);
		if (mutex->type == PTHREAD_MUTEX_ERRORCHECK ||
		    mutex->type == PTHREAD_MUTEX_RECURSIVE) {
			return (EPERM);
		} else {
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

	if (mutex->type == PTHREAD_MUTEX_RECURSIVE) {
		if (mutex->count > 0) {
			mutex->count--;
			return (0);
		}
	}

	mutex->owner = NULL;
	membar_exit_before_atomic();
	if (atomic_dec_int_nv(&mutex->lock) != UNLOCKED) {
		mutex->lock = UNLOCKED;
		_wake(&mutex->lock, 1);
	}

	return (0);
}
DEF_STRONG(pthread_mutex_unlock);
