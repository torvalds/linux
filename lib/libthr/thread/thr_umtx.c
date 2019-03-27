/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2005 David Xu <davidxu@freebsd.org>
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

#include "thr_private.h"
#include "thr_umtx.h"

#ifndef HAS__UMTX_OP_ERR
int _umtx_op_err(void *obj, int op, u_long val, void *uaddr, void *uaddr2)
{

	if (_umtx_op(obj, op, val, uaddr, uaddr2) == -1)
		return (errno);
	return (0);
}
#endif

void
_thr_umutex_init(struct umutex *mtx)
{
	static const struct umutex default_mtx = DEFAULT_UMUTEX;

	*mtx = default_mtx;
}

void
_thr_urwlock_init(struct urwlock *rwl)
{
	static const struct urwlock default_rwl = DEFAULT_URWLOCK;

	*rwl = default_rwl;
}

int
__thr_umutex_lock(struct umutex *mtx, uint32_t id)
{
	uint32_t owner;

	if ((mtx->m_flags & (UMUTEX_PRIO_PROTECT | UMUTEX_PRIO_INHERIT)) != 0)
		return	(_umtx_op_err(mtx, UMTX_OP_MUTEX_LOCK, 0, 0, 0));

	for (;;) {
		owner = mtx->m_owner;
		if ((owner & ~UMUTEX_CONTESTED) == 0 &&
		     atomic_cmpset_acq_32(&mtx->m_owner, owner, id | owner))
			return (0);
		if (owner == UMUTEX_RB_OWNERDEAD &&
		     atomic_cmpset_acq_32(&mtx->m_owner, owner,
		     id | UMUTEX_CONTESTED))
			return (EOWNERDEAD);
		if (owner == UMUTEX_RB_NOTRECOV)
			return (ENOTRECOVERABLE);

		/* wait in kernel */
		_umtx_op_err(mtx, UMTX_OP_MUTEX_WAIT, 0, 0, 0);
	}
}

#define SPINLOOPS 1000

int
__thr_umutex_lock_spin(struct umutex *mtx, uint32_t id)
{
	uint32_t owner;
	int count;

	if (!_thr_is_smp)
		return (__thr_umutex_lock(mtx, id));
	if ((mtx->m_flags & (UMUTEX_PRIO_PROTECT | UMUTEX_PRIO_INHERIT)) != 0)
		return	(_umtx_op_err(mtx, UMTX_OP_MUTEX_LOCK, 0, 0, 0));

	for (;;) {
		count = SPINLOOPS;
		while (count--) {
			owner = mtx->m_owner;
			if ((owner & ~UMUTEX_CONTESTED) == 0 &&
			    atomic_cmpset_acq_32(&mtx->m_owner, owner,
			    id | owner))
				return (0);
			if (__predict_false(owner == UMUTEX_RB_OWNERDEAD) &&
			    atomic_cmpset_acq_32(&mtx->m_owner, owner,
			    id | UMUTEX_CONTESTED))
				return (EOWNERDEAD);
			if (__predict_false(owner == UMUTEX_RB_NOTRECOV))
				return (ENOTRECOVERABLE);
			CPU_SPINWAIT;
		}

		/* wait in kernel */
		_umtx_op_err(mtx, UMTX_OP_MUTEX_WAIT, 0, 0, 0);
	}
}

int
__thr_umutex_timedlock(struct umutex *mtx, uint32_t id,
	const struct timespec *abstime)
{
	struct _umtx_time *tm_p, timeout;
	size_t tm_size;
	uint32_t owner;
	int ret;

	if (abstime == NULL) {
		tm_p = NULL;
		tm_size = 0;
	} else {
		timeout._clockid = CLOCK_REALTIME;
		timeout._flags = UMTX_ABSTIME;
		timeout._timeout = *abstime;
		tm_p = &timeout;
		tm_size = sizeof(timeout);
	}

	for (;;) {
		if ((mtx->m_flags & (UMUTEX_PRIO_PROTECT |
		    UMUTEX_PRIO_INHERIT)) == 0) {
			/* try to lock it */
			owner = mtx->m_owner;
			if ((owner & ~UMUTEX_CONTESTED) == 0 &&
			     atomic_cmpset_acq_32(&mtx->m_owner, owner,
			     id | owner))
				return (0);
			if (__predict_false(owner == UMUTEX_RB_OWNERDEAD) &&
			     atomic_cmpset_acq_32(&mtx->m_owner, owner,
			     id | UMUTEX_CONTESTED))
				return (EOWNERDEAD);
			if (__predict_false(owner == UMUTEX_RB_NOTRECOV))
				return (ENOTRECOVERABLE);
			/* wait in kernel */
			ret = _umtx_op_err(mtx, UMTX_OP_MUTEX_WAIT, 0,
			    (void *)tm_size, __DECONST(void *, tm_p));
		} else {
			ret = _umtx_op_err(mtx, UMTX_OP_MUTEX_LOCK, 0, 
			    (void *)tm_size, __DECONST(void *, tm_p));
			if (ret == 0 || ret == EOWNERDEAD ||
			    ret == ENOTRECOVERABLE)
				break;
		}
		if (ret == ETIMEDOUT)
			break;
	}
	return (ret);
}

int
__thr_umutex_unlock(struct umutex *mtx)
{

	return (_umtx_op_err(mtx, UMTX_OP_MUTEX_UNLOCK, 0, 0, 0));
}

int
__thr_umutex_trylock(struct umutex *mtx)
{

	return (_umtx_op_err(mtx, UMTX_OP_MUTEX_TRYLOCK, 0, 0, 0));
}

int
__thr_umutex_set_ceiling(struct umutex *mtx, uint32_t ceiling,
    uint32_t *oldceiling)
{

	return (_umtx_op_err(mtx, UMTX_OP_SET_CEILING, ceiling, oldceiling, 0));
}

int
_thr_umtx_wait(volatile long *mtx, long id, const struct timespec *timeout)
{

	if (timeout && (timeout->tv_sec < 0 || (timeout->tv_sec == 0 &&
	    timeout->tv_nsec <= 0)))
		return (ETIMEDOUT);
	return (_umtx_op_err(__DEVOLATILE(void *, mtx), UMTX_OP_WAIT, id, 0,
	    __DECONST(void*, timeout)));
}

int
_thr_umtx_wait_uint(volatile u_int *mtx, u_int id,
    const struct timespec *timeout, int shared)
{

	if (timeout && (timeout->tv_sec < 0 || (timeout->tv_sec == 0 &&
	    timeout->tv_nsec <= 0)))
		return (ETIMEDOUT);
	return (_umtx_op_err(__DEVOLATILE(void *, mtx), shared ?
	    UMTX_OP_WAIT_UINT : UMTX_OP_WAIT_UINT_PRIVATE, id, 0,
	    __DECONST(void*, timeout)));
}

int
_thr_umtx_timedwait_uint(volatile u_int *mtx, u_int id, int clockid,
    const struct timespec *abstime, int shared)
{
	struct _umtx_time *tm_p, timeout;
	size_t tm_size;

	if (abstime == NULL) {
		tm_p = NULL;
		tm_size = 0;
	} else {
		timeout._clockid = clockid;
		timeout._flags = UMTX_ABSTIME;
		timeout._timeout = *abstime;
		tm_p = &timeout;
		tm_size = sizeof(timeout);
	}

	return (_umtx_op_err(__DEVOLATILE(void *, mtx), shared ?
	    UMTX_OP_WAIT_UINT : UMTX_OP_WAIT_UINT_PRIVATE, id,
	    (void *)tm_size, __DECONST(void *, tm_p)));
}

int
_thr_umtx_wake(volatile void *mtx, int nr_wakeup, int shared)
{

	return (_umtx_op_err(__DEVOLATILE(void *, mtx), shared ?
	    UMTX_OP_WAKE : UMTX_OP_WAKE_PRIVATE, nr_wakeup, 0, 0));
}

void
_thr_ucond_init(struct ucond *cv)
{

	bzero(cv, sizeof(struct ucond));
}

int
_thr_ucond_wait(struct ucond *cv, struct umutex *m,
	const struct timespec *timeout, int flags)
{
	struct pthread *curthread;

	if (timeout && (timeout->tv_sec < 0 || (timeout->tv_sec == 0 &&
	    timeout->tv_nsec <= 0))) {
		curthread = _get_curthread();
		_thr_umutex_unlock(m, TID(curthread));
                return (ETIMEDOUT);
	}
	return (_umtx_op_err(cv, UMTX_OP_CV_WAIT, flags, m,
	    __DECONST(void*, timeout)));
}
 
int
_thr_ucond_signal(struct ucond *cv)
{

	if (!cv->c_has_waiters)
		return (0);
	return (_umtx_op_err(cv, UMTX_OP_CV_SIGNAL, 0, NULL, NULL));
}

int
_thr_ucond_broadcast(struct ucond *cv)
{

	if (!cv->c_has_waiters)
		return (0);
	return (_umtx_op_err(cv, UMTX_OP_CV_BROADCAST, 0, NULL, NULL));
}

int
__thr_rwlock_rdlock(struct urwlock *rwlock, int flags,
	const struct timespec *tsp)
{
	struct _umtx_time timeout, *tm_p;
	size_t tm_size;

	if (tsp == NULL) {
		tm_p = NULL;
		tm_size = 0;
	} else {
		timeout._timeout = *tsp;
		timeout._flags = UMTX_ABSTIME;
		timeout._clockid = CLOCK_REALTIME;
		tm_p = &timeout;
		tm_size = sizeof(timeout);
	}
	return (_umtx_op_err(rwlock, UMTX_OP_RW_RDLOCK, flags,
	    (void *)tm_size, tm_p));
}

int
__thr_rwlock_wrlock(struct urwlock *rwlock, const struct timespec *tsp)
{
	struct _umtx_time timeout, *tm_p;
	size_t tm_size;

	if (tsp == NULL) {
		tm_p = NULL;
		tm_size = 0;
	} else {
		timeout._timeout = *tsp;
		timeout._flags = UMTX_ABSTIME;
		timeout._clockid = CLOCK_REALTIME;
		tm_p = &timeout;
		tm_size = sizeof(timeout);
	}
	return (_umtx_op_err(rwlock, UMTX_OP_RW_WRLOCK, 0, (void *)tm_size,
	    tm_p));
}

int
__thr_rwlock_unlock(struct urwlock *rwlock)
{

	return (_umtx_op_err(rwlock, UMTX_OP_RW_UNLOCK, 0, NULL, NULL));
}

void
_thr_rwl_rdlock(struct urwlock *rwlock)
{
	int ret;

	for (;;) {
		if (_thr_rwlock_tryrdlock(rwlock, URWLOCK_PREFER_READER) == 0)
			return;
		ret = __thr_rwlock_rdlock(rwlock, URWLOCK_PREFER_READER, NULL);
		if (ret == 0)
			return;
		if (ret != EINTR)
			PANIC("rdlock error");
	}
}

void
_thr_rwl_wrlock(struct urwlock *rwlock)
{
	int ret;

	for (;;) {
		if (_thr_rwlock_trywrlock(rwlock) == 0)
			return;
		ret = __thr_rwlock_wrlock(rwlock, NULL);
		if (ret == 0)
			return;
		if (ret != EINTR)
			PANIC("wrlock error");
	}
}

void
_thr_rwl_unlock(struct urwlock *rwlock)
{

	if (_thr_rwlock_unlock(rwlock))
		PANIC("unlock error");
}
