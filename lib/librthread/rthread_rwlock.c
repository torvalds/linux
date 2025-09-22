/*	$OpenBSD: rthread_rwlock.c,v 1.13 2019/03/03 18:39:10 visa Exp $ */
/*
 * Copyright (c) 2019 Martin Pieuchot <mpi@openbsd.org>
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

#include <stdlib.h>
#include <unistd.h>
#include <errno.h>

#include <pthread.h>

#include "rthread.h"
#include "synch.h"

#define UNLOCKED	0
#define MAXREADER	0x7ffffffe
#define WRITER		0x7fffffff
#define WAITING		0x80000000
#define COUNT(v)	((v) & WRITER)

#define SPIN_COUNT	128
#if defined(__i386__) || defined(__amd64__)
#define SPIN_WAIT()	asm volatile("pause": : : "memory")
#else
#define SPIN_WAIT()	do { } while (0)
#endif

static _atomic_lock_t rwlock_init_lock = _SPINLOCK_UNLOCKED;

int
pthread_rwlock_init(pthread_rwlock_t *lockp,
    const pthread_rwlockattr_t *attrp __unused)
{
	pthread_rwlock_t rwlock;

	rwlock = calloc(1, sizeof(*rwlock));
	if (!rwlock)
		return (errno);

	*lockp = rwlock;

	return (0);
}
DEF_STD(pthread_rwlock_init);

int
pthread_rwlock_destroy(pthread_rwlock_t *lockp)
{
	pthread_rwlock_t rwlock;

	rwlock = *lockp;
	if (rwlock) {
		if (rwlock->value != UNLOCKED) {
#define MSG "pthread_rwlock_destroy on rwlock with waiters!\n"
			write(2, MSG, sizeof(MSG) - 1);
#undef MSG
			return (EBUSY);
		}
		free((void *)rwlock);
		*lockp = NULL;
	}

	return (0);
}

static int
_rthread_rwlock_ensure_init(pthread_rwlock_t *rwlockp)
{
	int ret = 0;

	/*
	 * If the rwlock is statically initialized, perform the dynamic
	 * initialization.
	 */
	if (*rwlockp == NULL) {
		_spinlock(&rwlock_init_lock);
		if (*rwlockp == NULL)
			ret = pthread_rwlock_init(rwlockp, NULL);
		_spinunlock(&rwlock_init_lock);
	}
	return (ret);
}

static int
_rthread_rwlock_tryrdlock(pthread_rwlock_t rwlock)
{
	unsigned int val;

	do {
		val = rwlock->value;
		if (COUNT(val) == WRITER)
			return (EBUSY);
		if (COUNT(val) == MAXREADER)
			return (EAGAIN);
	} while (atomic_cas_uint(&rwlock->value, val, val + 1) != val);

	membar_enter_after_atomic();
	return (0);
}

static int
_rthread_rwlock_timedrdlock(pthread_rwlock_t *rwlockp, int trywait,
    const struct timespec *abs, int timed)
{
	pthread_t self = pthread_self();
	pthread_rwlock_t rwlock;
	unsigned int val, new;
	int i, error;

	if ((error = _rthread_rwlock_ensure_init(rwlockp)))
		return (error);

	rwlock = *rwlockp;
	_rthread_debug(5, "%p: rwlock_%srdlock %p (%u)\n", self,
	    (timed ? "timed" : (trywait ? "try" : "")), (void *)rwlock,
	    rwlock->value);

	error = _rthread_rwlock_tryrdlock(rwlock);
	if (error != EBUSY || trywait)
		return (error);

	/* Try hard to not enter the kernel. */
	for (i = 0; i < SPIN_COUNT; i++) {
		val = rwlock->value;
		if (val == UNLOCKED || (val & WAITING))
			break;

		SPIN_WAIT();
	}

	while ((error = _rthread_rwlock_tryrdlock(rwlock)) == EBUSY) {
		val = rwlock->value;
		if (val == UNLOCKED || (COUNT(val)) != WRITER)
			continue;
		new = val | WAITING;
		if (atomic_cas_uint(&rwlock->value, val, new) == val) {
			error = _twait(&rwlock->value, new, CLOCK_REALTIME,
			    abs);
		}
		if (error == ETIMEDOUT)
			break;
	}

	return (error);

}

int
pthread_rwlock_tryrdlock(pthread_rwlock_t *rwlockp)
{
	return (_rthread_rwlock_timedrdlock(rwlockp, 1, NULL, 0));
}

int
pthread_rwlock_timedrdlock(pthread_rwlock_t *rwlockp,
    const struct timespec *abs)
{
	return (_rthread_rwlock_timedrdlock(rwlockp, 0, abs, 1));
}

int
pthread_rwlock_rdlock(pthread_rwlock_t *rwlockp)
{
	return (_rthread_rwlock_timedrdlock(rwlockp, 0, NULL, 0));
}

static int
_rthread_rwlock_tryrwlock(pthread_rwlock_t rwlock)
{
	if (atomic_cas_uint(&rwlock->value, UNLOCKED, WRITER) != UNLOCKED)
		return (EBUSY);

	membar_enter_after_atomic();
	return (0);
}


static int
_rthread_rwlock_timedwrlock(pthread_rwlock_t *rwlockp, int trywait,
    const struct timespec *abs, int timed)
{
	pthread_t self = pthread_self();
	pthread_rwlock_t rwlock;
	unsigned int val, new;
	int i, error;

	if ((error = _rthread_rwlock_ensure_init(rwlockp)))
		return (error);

	rwlock = *rwlockp;
	_rthread_debug(5, "%p: rwlock_%swrlock %p (%u)\n", self,
	    (timed ? "timed" : (trywait ? "try" : "")), (void *)rwlock,
	    rwlock->value);

	error = _rthread_rwlock_tryrwlock(rwlock);
	if (error != EBUSY || trywait)
		return (error);

	/* Try hard to not enter the kernel. */
	for (i = 0; i < SPIN_COUNT; i++) {
		val = rwlock->value;
		if (val == UNLOCKED || (val & WAITING))
			break;

		SPIN_WAIT();
	}

	while ((error = _rthread_rwlock_tryrwlock(rwlock)) == EBUSY) {
		val = rwlock->value;
		if (val == UNLOCKED)
			continue;
		new = val | WAITING;
		if (atomic_cas_uint(&rwlock->value, val, new) == val) {
			error = _twait(&rwlock->value, new, CLOCK_REALTIME,
			    abs);
		}
		if (error == ETIMEDOUT)
			break;
	}

	return (error);
}

int
pthread_rwlock_trywrlock(pthread_rwlock_t *rwlockp)
{
	return (_rthread_rwlock_timedwrlock(rwlockp, 1, NULL, 0));
}

int
pthread_rwlock_timedwrlock(pthread_rwlock_t *rwlockp,
    const struct timespec *abs)
{
	return (_rthread_rwlock_timedwrlock(rwlockp, 0, abs, 1));
}

int
pthread_rwlock_wrlock(pthread_rwlock_t *rwlockp)
{
	return (_rthread_rwlock_timedwrlock(rwlockp, 0, NULL, 0));
}

int
pthread_rwlock_unlock(pthread_rwlock_t *rwlockp)
{
	pthread_t self = pthread_self();
	pthread_rwlock_t rwlock;
	unsigned int val, new;

	rwlock = *rwlockp;
	_rthread_debug(5, "%p: rwlock_unlock %p\n", self, (void *)rwlock);

	membar_exit_before_atomic();
	do {
		val = rwlock->value;
		if (COUNT(val) == WRITER || COUNT(val) == 1)
			new = UNLOCKED;
		else
			new = val - 1;
	} while (atomic_cas_uint(&rwlock->value, val, new) != val);

	if (new == UNLOCKED && (val & WAITING))
		_wake(&rwlock->value, INT_MAX);

	return (0);
}
