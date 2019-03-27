/*-
 * SPDX-License-Identifier: BSD-4-Clause
 *
 * Copyright (c) 1995 John Birrell <jb@cimlogic.com.au>.
 * Copyright (c) 2006 David Xu <davidxu@freebsd.org>.
 * Copyright (c) 2015, 2016 The FreeBSD Foundation
 *
 * All rights reserved.
 *
 * Portions of this software were developed by Konstantin Belousov
 * under sponsorship from the FreeBSD Foundation.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *	This product includes software developed by John Birrell.
 * 4. Neither the name of the author nor the names of any co-contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY JOHN BIRRELL AND CONTRIBUTORS ``AS IS'' AND
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

#include "namespace.h"
#include <stdlib.h>
#include <errno.h>
#include <string.h>
#include <sys/param.h>
#include <sys/queue.h>
#include <pthread.h>
#include <pthread_np.h>
#include "un-namespace.h"

#include "thr_private.h"

_Static_assert(sizeof(struct pthread_mutex) <= PAGE_SIZE,
    "pthread_mutex is too large for off-page");

/*
 * For adaptive mutexes, how many times to spin doing trylock2
 * before entering the kernel to block
 */
#define MUTEX_ADAPTIVE_SPINS	2000

/*
 * Prototypes
 */
int	__pthread_mutex_consistent(pthread_mutex_t *mutex);
int	__pthread_mutex_init(pthread_mutex_t * __restrict mutex,
		const pthread_mutexattr_t * __restrict mutex_attr);
int	__pthread_mutex_trylock(pthread_mutex_t *mutex);
int	__pthread_mutex_lock(pthread_mutex_t *mutex);
int	__pthread_mutex_timedlock(pthread_mutex_t * __restrict mutex,
		const struct timespec * __restrict abstime);
int	_pthread_mutex_getspinloops_np(pthread_mutex_t *mutex, int *count);
int	_pthread_mutex_setspinloops_np(pthread_mutex_t *mutex, int count);
int	__pthread_mutex_setspinloops_np(pthread_mutex_t *mutex, int count);
int	_pthread_mutex_setyieldloops_np(pthread_mutex_t *mutex, int count);
int	_pthread_mutex_getyieldloops_np(pthread_mutex_t *mutex, int *count);
int	__pthread_mutex_setyieldloops_np(pthread_mutex_t *mutex, int count);

static int	mutex_self_trylock(pthread_mutex_t);
static int	mutex_self_lock(pthread_mutex_t,
				const struct timespec *abstime);
static int	mutex_unlock_common(struct pthread_mutex *, bool, int *);
static int	mutex_lock_sleep(struct pthread *, pthread_mutex_t,
				const struct timespec *);
static void	mutex_init_robust(struct pthread *curthread);
static int	mutex_qidx(struct pthread_mutex *m);
static bool	is_robust_mutex(struct pthread_mutex *m);
static bool	is_pshared_mutex(struct pthread_mutex *m);

__weak_reference(__pthread_mutex_init, pthread_mutex_init);
__strong_reference(__pthread_mutex_init, _pthread_mutex_init);
__weak_reference(__pthread_mutex_lock, pthread_mutex_lock);
__strong_reference(__pthread_mutex_lock, _pthread_mutex_lock);
__weak_reference(__pthread_mutex_timedlock, pthread_mutex_timedlock);
__strong_reference(__pthread_mutex_timedlock, _pthread_mutex_timedlock);
__weak_reference(__pthread_mutex_trylock, pthread_mutex_trylock);
__strong_reference(__pthread_mutex_trylock, _pthread_mutex_trylock);
__weak_reference(_pthread_mutex_consistent, pthread_mutex_consistent);
__strong_reference(_pthread_mutex_consistent, __pthread_mutex_consistent);

/* Single underscore versions provided for libc internal usage: */
/* No difference between libc and application usage of these: */
__weak_reference(_pthread_mutex_destroy, pthread_mutex_destroy);
__weak_reference(_pthread_mutex_unlock, pthread_mutex_unlock);

__weak_reference(_pthread_mutex_getprioceiling, pthread_mutex_getprioceiling);
__weak_reference(_pthread_mutex_setprioceiling, pthread_mutex_setprioceiling);

__weak_reference(__pthread_mutex_setspinloops_np, pthread_mutex_setspinloops_np);
__strong_reference(__pthread_mutex_setspinloops_np, _pthread_mutex_setspinloops_np);
__weak_reference(_pthread_mutex_getspinloops_np, pthread_mutex_getspinloops_np);

__weak_reference(__pthread_mutex_setyieldloops_np, pthread_mutex_setyieldloops_np);
__strong_reference(__pthread_mutex_setyieldloops_np, _pthread_mutex_setyieldloops_np);
__weak_reference(_pthread_mutex_getyieldloops_np, pthread_mutex_getyieldloops_np);
__weak_reference(_pthread_mutex_isowned_np, pthread_mutex_isowned_np);

static void
mutex_init_link(struct pthread_mutex *m)
{

#if defined(_PTHREADS_INVARIANTS)
	m->m_qe.tqe_prev = NULL;
	m->m_qe.tqe_next = NULL;
	m->m_pqe.tqe_prev = NULL;
	m->m_pqe.tqe_next = NULL;
#endif
}

static void
mutex_assert_is_owned(struct pthread_mutex *m __unused)
{

#if defined(_PTHREADS_INVARIANTS)
	if (__predict_false(m->m_qe.tqe_prev == NULL))
		PANIC("mutex %p own %#x is not on list %p %p",
		    m, m->m_lock.m_owner, m->m_qe.tqe_prev, m->m_qe.tqe_next);
#endif
}

static void
mutex_assert_not_owned(struct pthread *curthread __unused,
    struct pthread_mutex *m __unused)
{

#if defined(_PTHREADS_INVARIANTS)
	if (__predict_false(m->m_qe.tqe_prev != NULL ||
	    m->m_qe.tqe_next != NULL))
		PANIC("mutex %p own %#x is on list %p %p",
		    m, m->m_lock.m_owner, m->m_qe.tqe_prev, m->m_qe.tqe_next);
	if (__predict_false(is_robust_mutex(m) &&
	    (m->m_lock.m_rb_lnk != 0 || m->m_rb_prev != NULL ||
	    (is_pshared_mutex(m) && curthread->robust_list ==
	    (uintptr_t)&m->m_lock) ||
	    (!is_pshared_mutex(m) && curthread->priv_robust_list ==
	    (uintptr_t)&m->m_lock))))
		PANIC(
    "mutex %p own %#x is on robust linkage %p %p head %p phead %p",
		    m, m->m_lock.m_owner, (void *)m->m_lock.m_rb_lnk,
		    m->m_rb_prev, (void *)curthread->robust_list,
		    (void *)curthread->priv_robust_list);
#endif
}

static bool
is_pshared_mutex(struct pthread_mutex *m)
{

	return ((m->m_lock.m_flags & USYNC_PROCESS_SHARED) != 0);
}

static bool
is_robust_mutex(struct pthread_mutex *m)
{

	return ((m->m_lock.m_flags & UMUTEX_ROBUST) != 0);
}

int
_mutex_enter_robust(struct pthread *curthread, struct pthread_mutex *m)
{

#if defined(_PTHREADS_INVARIANTS)
	if (__predict_false(curthread->inact_mtx != 0))
		PANIC("inact_mtx enter");
#endif
	if (!is_robust_mutex(m))
		return (0);

	mutex_init_robust(curthread);
	curthread->inact_mtx = (uintptr_t)&m->m_lock;
	return (1);
}

void
_mutex_leave_robust(struct pthread *curthread, struct pthread_mutex *m __unused)
{

#if defined(_PTHREADS_INVARIANTS)
	if (__predict_false(curthread->inact_mtx != (uintptr_t)&m->m_lock))
		PANIC("inact_mtx leave");
#endif
	curthread->inact_mtx = 0;
}

static int
mutex_check_attr(const struct pthread_mutex_attr *attr)
{

	if (attr->m_type < PTHREAD_MUTEX_ERRORCHECK ||
	    attr->m_type >= PTHREAD_MUTEX_TYPE_MAX)
		return (EINVAL);
	if (attr->m_protocol < PTHREAD_PRIO_NONE ||
	    attr->m_protocol > PTHREAD_PRIO_PROTECT)
		return (EINVAL);
	return (0);
}

static void
mutex_init_robust(struct pthread *curthread)
{
	struct umtx_robust_lists_params rb;

	if (curthread == NULL)
		curthread = _get_curthread();
	if (curthread->robust_inited)
		return;
	rb.robust_list_offset = (uintptr_t)&curthread->robust_list;
	rb.robust_priv_list_offset = (uintptr_t)&curthread->priv_robust_list;
	rb.robust_inact_offset = (uintptr_t)&curthread->inact_mtx;
	_umtx_op(NULL, UMTX_OP_ROBUST_LISTS, sizeof(rb), &rb, NULL);
	curthread->robust_inited = 1;
}

static void
mutex_init_body(struct pthread_mutex *pmutex,
    const struct pthread_mutex_attr *attr)
{

	pmutex->m_flags = attr->m_type;
	pmutex->m_count = 0;
	pmutex->m_spinloops = 0;
	pmutex->m_yieldloops = 0;
	mutex_init_link(pmutex);
	switch (attr->m_protocol) {
	case PTHREAD_PRIO_NONE:
		pmutex->m_lock.m_owner = UMUTEX_UNOWNED;
		pmutex->m_lock.m_flags = 0;
		break;
	case PTHREAD_PRIO_INHERIT:
		pmutex->m_lock.m_owner = UMUTEX_UNOWNED;
		pmutex->m_lock.m_flags = UMUTEX_PRIO_INHERIT;
		break;
	case PTHREAD_PRIO_PROTECT:
		pmutex->m_lock.m_owner = UMUTEX_CONTESTED;
		pmutex->m_lock.m_flags = UMUTEX_PRIO_PROTECT;
		pmutex->m_lock.m_ceilings[0] = attr->m_ceiling;
		break;
	}
	if (attr->m_pshared == PTHREAD_PROCESS_SHARED)
		pmutex->m_lock.m_flags |= USYNC_PROCESS_SHARED;
	if (attr->m_robust == PTHREAD_MUTEX_ROBUST) {
		mutex_init_robust(NULL);
		pmutex->m_lock.m_flags |= UMUTEX_ROBUST;
	}
	if (PMUTEX_TYPE(pmutex->m_flags) == PTHREAD_MUTEX_ADAPTIVE_NP) {
		pmutex->m_spinloops =
		    _thr_spinloops ? _thr_spinloops: MUTEX_ADAPTIVE_SPINS;
		pmutex->m_yieldloops = _thr_yieldloops;
	}
}

static int
mutex_init(pthread_mutex_t *mutex,
    const struct pthread_mutex_attr *mutex_attr,
    void *(calloc_cb)(size_t, size_t))
{
	const struct pthread_mutex_attr *attr;
	struct pthread_mutex *pmutex;
	int error;

	if (mutex_attr == NULL) {
		attr = &_pthread_mutexattr_default;
	} else {
		attr = mutex_attr;
		error = mutex_check_attr(attr);
		if (error != 0)
			return (error);
	}
	if ((pmutex = (pthread_mutex_t)
		calloc_cb(1, sizeof(struct pthread_mutex))) == NULL)
		return (ENOMEM);
	mutex_init_body(pmutex, attr);
	*mutex = pmutex;
	return (0);
}

static int
init_static(struct pthread *thread, pthread_mutex_t *mutex)
{
	int ret;

	THR_LOCK_ACQUIRE(thread, &_mutex_static_lock);

	if (*mutex == THR_MUTEX_INITIALIZER)
		ret = mutex_init(mutex, &_pthread_mutexattr_default,
		    __thr_calloc);
	else if (*mutex == THR_ADAPTIVE_MUTEX_INITIALIZER)
		ret = mutex_init(mutex, &_pthread_mutexattr_adaptive_default,
		    __thr_calloc);
	else
		ret = 0;
	THR_LOCK_RELEASE(thread, &_mutex_static_lock);

	return (ret);
}

static void
set_inherited_priority(struct pthread *curthread, struct pthread_mutex *m)
{
	struct pthread_mutex *m2;

	m2 = TAILQ_LAST(&curthread->mq[mutex_qidx(m)], mutex_queue);
	if (m2 != NULL)
		m->m_lock.m_ceilings[1] = m2->m_lock.m_ceilings[0];
	else
		m->m_lock.m_ceilings[1] = -1;
}

static void
shared_mutex_init(struct pthread_mutex *pmtx, const struct
    pthread_mutex_attr *mutex_attr)
{
	static const struct pthread_mutex_attr foobar_mutex_attr = {
		.m_type = PTHREAD_MUTEX_DEFAULT,
		.m_protocol = PTHREAD_PRIO_NONE,
		.m_ceiling = 0,
		.m_pshared = PTHREAD_PROCESS_SHARED,
		.m_robust = PTHREAD_MUTEX_STALLED,
	};
	bool done;

	/*
	 * Hack to allow multiple pthread_mutex_init() calls on the
	 * same process-shared mutex.  We rely on kernel allocating
	 * zeroed offpage for the mutex, i.e. the
	 * PMUTEX_INITSTAGE_ALLOC value must be zero.
	 */
	for (done = false; !done;) {
		switch (pmtx->m_ps) {
		case PMUTEX_INITSTAGE_DONE:
			atomic_thread_fence_acq();
			done = true;
			break;
		case PMUTEX_INITSTAGE_ALLOC:
			if (atomic_cmpset_int(&pmtx->m_ps,
			    PMUTEX_INITSTAGE_ALLOC, PMUTEX_INITSTAGE_BUSY)) {
				if (mutex_attr == NULL)
					mutex_attr = &foobar_mutex_attr;
				mutex_init_body(pmtx, mutex_attr);
				atomic_store_rel_int(&pmtx->m_ps,
				    PMUTEX_INITSTAGE_DONE);
				done = true;
			}
			break;
		case PMUTEX_INITSTAGE_BUSY:
			_pthread_yield();
			break;
		default:
			PANIC("corrupted offpage");
			break;
		}
	}
}

int
__pthread_mutex_init(pthread_mutex_t * __restrict mutex,
    const pthread_mutexattr_t * __restrict mutex_attr)
{
	struct pthread_mutex *pmtx;
	int ret;

	if (mutex_attr != NULL) {
		ret = mutex_check_attr(*mutex_attr);
		if (ret != 0)
			return (ret);
	}
	if (mutex_attr == NULL ||
	    (*mutex_attr)->m_pshared == PTHREAD_PROCESS_PRIVATE) {
		__thr_malloc_init();
		return (mutex_init(mutex, mutex_attr ? *mutex_attr : NULL,
		    __thr_calloc));
	}
	pmtx = __thr_pshared_offpage(__DECONST(void *, mutex), 1);
	if (pmtx == NULL)
		return (EFAULT);
	*mutex = THR_PSHARED_PTR;
	shared_mutex_init(pmtx, *mutex_attr);
	return (0);
}

/* This function is used internally by malloc. */
int
_pthread_mutex_init_calloc_cb(pthread_mutex_t *mutex,
    void *(calloc_cb)(size_t, size_t))
{
	static const struct pthread_mutex_attr attr = {
		.m_type = PTHREAD_MUTEX_NORMAL,
		.m_protocol = PTHREAD_PRIO_NONE,
		.m_ceiling = 0,
		.m_pshared = PTHREAD_PROCESS_PRIVATE,
		.m_robust = PTHREAD_MUTEX_STALLED,
	};
	int ret;

	ret = mutex_init(mutex, &attr, calloc_cb);
	if (ret == 0)
		(*mutex)->m_flags |= PMUTEX_FLAG_PRIVATE;
	return (ret);
}

/*
 * Fix mutex ownership for child process.
 *
 * Process private mutex ownership is transmitted from the forking
 * thread to the child process.
 *
 * Process shared mutex should not be inherited because owner is
 * forking thread which is in parent process, they are removed from
 * the owned mutex list.
 */
static void
queue_fork(struct pthread *curthread, struct mutex_queue *q,
    struct mutex_queue *qp, uint bit)
{
	struct pthread_mutex *m;

	TAILQ_INIT(q);
	TAILQ_FOREACH(m, qp, m_pqe) {
		TAILQ_INSERT_TAIL(q, m, m_qe);
		m->m_lock.m_owner = TID(curthread) | bit;
	}
}

void
_mutex_fork(struct pthread *curthread)
{

	queue_fork(curthread, &curthread->mq[TMQ_NORM],
	    &curthread->mq[TMQ_NORM_PRIV], 0);
	queue_fork(curthread, &curthread->mq[TMQ_NORM_PP],
	    &curthread->mq[TMQ_NORM_PP_PRIV], UMUTEX_CONTESTED);
	queue_fork(curthread, &curthread->mq[TMQ_ROBUST_PP],
	    &curthread->mq[TMQ_ROBUST_PP_PRIV], UMUTEX_CONTESTED);
	curthread->robust_list = 0;
}

int
_pthread_mutex_destroy(pthread_mutex_t *mutex)
{
	pthread_mutex_t m, m1;
	int ret;

	m = *mutex;
	if (m < THR_MUTEX_DESTROYED) {
		ret = 0;
	} else if (m == THR_MUTEX_DESTROYED) {
		ret = EINVAL;
	} else {
		if (m == THR_PSHARED_PTR) {
			m1 = __thr_pshared_offpage(mutex, 0);
			if (m1 != NULL) {
				mutex_assert_not_owned(_get_curthread(), m1);
				__thr_pshared_destroy(mutex);
			}
			*mutex = THR_MUTEX_DESTROYED;
			return (0);
		}
		if (PMUTEX_OWNER_ID(m) != 0 &&
		    (uint32_t)m->m_lock.m_owner != UMUTEX_RB_NOTRECOV) {
			ret = EBUSY;
		} else {
			*mutex = THR_MUTEX_DESTROYED;
			mutex_assert_not_owned(_get_curthread(), m);
			__thr_free(m);
			ret = 0;
		}
	}

	return (ret);
}

static int
mutex_qidx(struct pthread_mutex *m)
{

	if ((m->m_lock.m_flags & UMUTEX_PRIO_PROTECT) == 0)
		return (TMQ_NORM);
	return (is_robust_mutex(m) ? TMQ_ROBUST_PP : TMQ_NORM_PP);
}

/*
 * Both enqueue_mutex() and dequeue_mutex() operate on the
 * thread-private linkage of the locked mutexes and on the robust
 * linkage.
 *
 * Robust list, as seen by kernel, must be consistent even in the case
 * of thread termination at arbitrary moment.  Since either enqueue or
 * dequeue for list walked by kernel consists of rewriting a single
 * forward pointer, it is safe.  On the other hand, rewrite of the
 * back pointer is not atomic WRT the forward one, but kernel does not
 * care.
 */
static void
enqueue_mutex(struct pthread *curthread, struct pthread_mutex *m,
    int error)
{
	struct pthread_mutex *m1;
	uintptr_t *rl;
	int qidx;

	/* Add to the list of owned mutexes: */
	if (error != EOWNERDEAD)
		mutex_assert_not_owned(curthread, m);
	qidx = mutex_qidx(m);
	TAILQ_INSERT_TAIL(&curthread->mq[qidx], m, m_qe);
	if (!is_pshared_mutex(m))
		TAILQ_INSERT_TAIL(&curthread->mq[qidx + 1], m, m_pqe);
	if (is_robust_mutex(m)) {
		rl = is_pshared_mutex(m) ? &curthread->robust_list :
		    &curthread->priv_robust_list;
		m->m_rb_prev = NULL;
		if (*rl != 0) {
			m1 = __containerof((void *)*rl,
			    struct pthread_mutex, m_lock);
			m->m_lock.m_rb_lnk = (uintptr_t)&m1->m_lock;
			m1->m_rb_prev = m;
		} else {
			m1 = NULL;
			m->m_lock.m_rb_lnk = 0;
		}
		*rl = (uintptr_t)&m->m_lock;
	}
}

static void
dequeue_mutex(struct pthread *curthread, struct pthread_mutex *m)
{
	struct pthread_mutex *mp, *mn;
	int qidx;

	mutex_assert_is_owned(m);
	qidx = mutex_qidx(m);
	if (is_robust_mutex(m)) {
		mp = m->m_rb_prev;
		if (mp == NULL) {
			if (is_pshared_mutex(m)) {
				curthread->robust_list = m->m_lock.m_rb_lnk;
			} else {
				curthread->priv_robust_list =
				    m->m_lock.m_rb_lnk;
			}
		} else {
			mp->m_lock.m_rb_lnk = m->m_lock.m_rb_lnk;
		}
		if (m->m_lock.m_rb_lnk != 0) {
			mn = __containerof((void *)m->m_lock.m_rb_lnk,
			    struct pthread_mutex, m_lock);
			mn->m_rb_prev = m->m_rb_prev;
		}
		m->m_lock.m_rb_lnk = 0;
		m->m_rb_prev = NULL;
	}
	TAILQ_REMOVE(&curthread->mq[qidx], m, m_qe);
	if (!is_pshared_mutex(m))
		TAILQ_REMOVE(&curthread->mq[qidx + 1], m, m_pqe);
	if ((m->m_lock.m_flags & UMUTEX_PRIO_PROTECT) != 0)
		set_inherited_priority(curthread, m);
	mutex_init_link(m);
}

static int
check_and_init_mutex(pthread_mutex_t *mutex, struct pthread_mutex **m)
{
	int ret;

	*m = *mutex;
	ret = 0;
	if (*m == THR_PSHARED_PTR) {
		*m = __thr_pshared_offpage(mutex, 0);
		if (*m == NULL)
			ret = EINVAL;
		else
			shared_mutex_init(*m, NULL);
	} else if (__predict_false(*m <= THR_MUTEX_DESTROYED)) {
		if (*m == THR_MUTEX_DESTROYED) {
			ret = EINVAL;
		} else {
			ret = init_static(_get_curthread(), mutex);
			if (ret == 0)
				*m = *mutex;
		}
	}
	return (ret);
}

int
__pthread_mutex_trylock(pthread_mutex_t *mutex)
{
	struct pthread *curthread;
	struct pthread_mutex *m;
	uint32_t id;
	int ret, robust;

	ret = check_and_init_mutex(mutex, &m);
	if (ret != 0)
		return (ret);
	curthread = _get_curthread();
	id = TID(curthread);
	if (m->m_flags & PMUTEX_FLAG_PRIVATE)
		THR_CRITICAL_ENTER(curthread);
	robust = _mutex_enter_robust(curthread, m);
	ret = _thr_umutex_trylock(&m->m_lock, id);
	if (__predict_true(ret == 0) || ret == EOWNERDEAD) {
		enqueue_mutex(curthread, m, ret);
		if (ret == EOWNERDEAD)
			m->m_lock.m_flags |= UMUTEX_NONCONSISTENT;
	} else if (PMUTEX_OWNER_ID(m) == id) {
		ret = mutex_self_trylock(m);
	} /* else {} */
	if (robust)
		_mutex_leave_robust(curthread, m);
	if (ret != 0 && ret != EOWNERDEAD &&
	    (m->m_flags & PMUTEX_FLAG_PRIVATE) != 0)
		THR_CRITICAL_LEAVE(curthread);
	return (ret);
}

static int
mutex_lock_sleep(struct pthread *curthread, struct pthread_mutex *m,
    const struct timespec *abstime)
{
	uint32_t id, owner;
	int count, ret;

	id = TID(curthread);
	if (PMUTEX_OWNER_ID(m) == id)
		return (mutex_self_lock(m, abstime));

	/*
	 * For adaptive mutexes, spin for a bit in the expectation
	 * that if the application requests this mutex type then
	 * the lock is likely to be released quickly and it is
	 * faster than entering the kernel
	 */
	if (__predict_false((m->m_lock.m_flags & (UMUTEX_PRIO_PROTECT |
	    UMUTEX_PRIO_INHERIT | UMUTEX_ROBUST | UMUTEX_NONCONSISTENT)) != 0))
		goto sleep_in_kernel;

	if (!_thr_is_smp)
		goto yield_loop;

	count = m->m_spinloops;
	while (count--) {
		owner = m->m_lock.m_owner;
		if ((owner & ~UMUTEX_CONTESTED) == 0) {
			if (atomic_cmpset_acq_32(&m->m_lock.m_owner, owner,
			    id | owner)) {
				ret = 0;
				goto done;
			}
		}
		CPU_SPINWAIT;
	}

yield_loop:
	count = m->m_yieldloops;
	while (count--) {
		_sched_yield();
		owner = m->m_lock.m_owner;
		if ((owner & ~UMUTEX_CONTESTED) == 0) {
			if (atomic_cmpset_acq_32(&m->m_lock.m_owner, owner,
			    id | owner)) {
				ret = 0;
				goto done;
			}
		}
	}

sleep_in_kernel:
	if (abstime == NULL)
		ret = __thr_umutex_lock(&m->m_lock, id);
	else if (__predict_false(abstime->tv_nsec < 0 ||
	    abstime->tv_nsec >= 1000000000))
		ret = EINVAL;
	else
		ret = __thr_umutex_timedlock(&m->m_lock, id, abstime);
done:
	if (ret == 0 || ret == EOWNERDEAD) {
		enqueue_mutex(curthread, m, ret);
		if (ret == EOWNERDEAD)
			m->m_lock.m_flags |= UMUTEX_NONCONSISTENT;
	}
	return (ret);
}

static inline int
mutex_lock_common(struct pthread_mutex *m, const struct timespec *abstime,
    bool cvattach, bool rb_onlist)
{
	struct pthread *curthread;
	int ret, robust;

	robust = 0;  /* pacify gcc */
	curthread  = _get_curthread();
	if (!cvattach && m->m_flags & PMUTEX_FLAG_PRIVATE)
		THR_CRITICAL_ENTER(curthread);
	if (!rb_onlist)
		robust = _mutex_enter_robust(curthread, m);
	ret = _thr_umutex_trylock2(&m->m_lock, TID(curthread));
	if (ret == 0 || ret == EOWNERDEAD) {
		enqueue_mutex(curthread, m, ret);
		if (ret == EOWNERDEAD)
			m->m_lock.m_flags |= UMUTEX_NONCONSISTENT;
	} else {
		ret = mutex_lock_sleep(curthread, m, abstime);
	}
	if (!rb_onlist && robust)
		_mutex_leave_robust(curthread, m);
	if (ret != 0 && ret != EOWNERDEAD &&
	    (m->m_flags & PMUTEX_FLAG_PRIVATE) != 0 && !cvattach)
		THR_CRITICAL_LEAVE(curthread);
	return (ret);
}

int
__pthread_mutex_lock(pthread_mutex_t *mutex)
{
	struct pthread_mutex *m;
	int ret;

	_thr_check_init();
	ret = check_and_init_mutex(mutex, &m);
	if (ret == 0)
		ret = mutex_lock_common(m, NULL, false, false);
	return (ret);
}

int
__pthread_mutex_timedlock(pthread_mutex_t * __restrict mutex,
    const struct timespec * __restrict abstime)
{
	struct pthread_mutex *m;
	int ret;

	_thr_check_init();
	ret = check_and_init_mutex(mutex, &m);
	if (ret == 0)
		ret = mutex_lock_common(m, abstime, false, false);
	return (ret);
}

int
_pthread_mutex_unlock(pthread_mutex_t *mutex)
{
	struct pthread_mutex *mp;

	if (*mutex == THR_PSHARED_PTR) {
		mp = __thr_pshared_offpage(mutex, 0);
		if (mp == NULL)
			return (EINVAL);
		shared_mutex_init(mp, NULL);
	} else {
		mp = *mutex;
	}
	return (mutex_unlock_common(mp, false, NULL));
}

int
_mutex_cv_lock(struct pthread_mutex *m, int count, bool rb_onlist)
{
	int error;

	error = mutex_lock_common(m, NULL, true, rb_onlist);
	if (error == 0 || error == EOWNERDEAD)
		m->m_count = count;
	return (error);
}

int
_mutex_cv_unlock(struct pthread_mutex *m, int *count, int *defer)
{

	/*
	 * Clear the count in case this is a recursive mutex.
	 */
	*count = m->m_count;
	m->m_count = 0;
	(void)mutex_unlock_common(m, true, defer);
        return (0);
}

int
_mutex_cv_attach(struct pthread_mutex *m, int count)
{
	struct pthread *curthread;

	curthread = _get_curthread();
	enqueue_mutex(curthread, m, 0);
	m->m_count = count;
	return (0);
}

int
_mutex_cv_detach(struct pthread_mutex *mp, int *recurse)
{
	struct pthread *curthread;
	int deferred, error;

	curthread = _get_curthread();
	if ((error = _mutex_owned(curthread, mp)) != 0)
		return (error);

	/*
	 * Clear the count in case this is a recursive mutex.
	 */
	*recurse = mp->m_count;
	mp->m_count = 0;
	dequeue_mutex(curthread, mp);

	/* Will this happen in real-world ? */
        if ((mp->m_flags & PMUTEX_FLAG_DEFERRED) != 0) {
		deferred = 1;
		mp->m_flags &= ~PMUTEX_FLAG_DEFERRED;
	} else
		deferred = 0;

	if (deferred)  {
		_thr_wake_all(curthread->defer_waiters,
		    curthread->nwaiter_defer);
		curthread->nwaiter_defer = 0;
	}
	return (0);
}

static int
mutex_self_trylock(struct pthread_mutex *m)
{
	int ret;

	switch (PMUTEX_TYPE(m->m_flags)) {
	case PTHREAD_MUTEX_ERRORCHECK:
	case PTHREAD_MUTEX_NORMAL:
	case PTHREAD_MUTEX_ADAPTIVE_NP:
		ret = EBUSY;
		break;

	case PTHREAD_MUTEX_RECURSIVE:
		/* Increment the lock count: */
		if (m->m_count + 1 > 0) {
			m->m_count++;
			ret = 0;
		} else
			ret = EAGAIN;
		break;

	default:
		/* Trap invalid mutex types; */
		ret = EINVAL;
	}

	return (ret);
}

static int
mutex_self_lock(struct pthread_mutex *m, const struct timespec *abstime)
{
	struct timespec	ts1, ts2;
	int ret;

	switch (PMUTEX_TYPE(m->m_flags)) {
	case PTHREAD_MUTEX_ERRORCHECK:
	case PTHREAD_MUTEX_ADAPTIVE_NP:
		if (abstime) {
			if (abstime->tv_sec < 0 || abstime->tv_nsec < 0 ||
			    abstime->tv_nsec >= 1000000000) {
				ret = EINVAL;
			} else {
				clock_gettime(CLOCK_REALTIME, &ts1);
				TIMESPEC_SUB(&ts2, abstime, &ts1);
				__sys_nanosleep(&ts2, NULL);
				ret = ETIMEDOUT;
			}
		} else {
			/*
			 * POSIX specifies that mutexes should return
			 * EDEADLK if a recursive lock is detected.
			 */
			ret = EDEADLK; 
		}
		break;

	case PTHREAD_MUTEX_NORMAL:
		/*
		 * What SS2 define as a 'normal' mutex.  Intentionally
		 * deadlock on attempts to get a lock you already own.
		 */
		ret = 0;
		if (abstime) {
			if (abstime->tv_sec < 0 || abstime->tv_nsec < 0 ||
			    abstime->tv_nsec >= 1000000000) {
				ret = EINVAL;
			} else {
				clock_gettime(CLOCK_REALTIME, &ts1);
				TIMESPEC_SUB(&ts2, abstime, &ts1);
				__sys_nanosleep(&ts2, NULL);
				ret = ETIMEDOUT;
			}
		} else {
			ts1.tv_sec = 30;
			ts1.tv_nsec = 0;
			for (;;)
				__sys_nanosleep(&ts1, NULL);
		}
		break;

	case PTHREAD_MUTEX_RECURSIVE:
		/* Increment the lock count: */
		if (m->m_count + 1 > 0) {
			m->m_count++;
			ret = 0;
		} else
			ret = EAGAIN;
		break;

	default:
		/* Trap invalid mutex types; */
		ret = EINVAL;
	}

	return (ret);
}

static int
mutex_unlock_common(struct pthread_mutex *m, bool cv, int *mtx_defer)
{
	struct pthread *curthread;
	uint32_t id;
	int deferred, error, robust;

	if (__predict_false(m <= THR_MUTEX_DESTROYED)) {
		if (m == THR_MUTEX_DESTROYED)
			return (EINVAL);
		return (EPERM);
	}

	curthread = _get_curthread();
	id = TID(curthread);

	/*
	 * Check if the running thread is not the owner of the mutex.
	 */
	if (__predict_false(PMUTEX_OWNER_ID(m) != id))
		return (EPERM);

	error = 0;
	if (__predict_false(PMUTEX_TYPE(m->m_flags) ==
	    PTHREAD_MUTEX_RECURSIVE && m->m_count > 0)) {
		m->m_count--;
	} else {
		if ((m->m_flags & PMUTEX_FLAG_DEFERRED) != 0) {
			deferred = 1;
			m->m_flags &= ~PMUTEX_FLAG_DEFERRED;
        	} else
			deferred = 0;

		robust = _mutex_enter_robust(curthread, m);
		dequeue_mutex(curthread, m);
		error = _thr_umutex_unlock2(&m->m_lock, id, mtx_defer);
		if (deferred)  {
			if (mtx_defer == NULL) {
				_thr_wake_all(curthread->defer_waiters,
				    curthread->nwaiter_defer);
				curthread->nwaiter_defer = 0;
			} else
				*mtx_defer = 1;
		}
		if (robust)
			_mutex_leave_robust(curthread, m);
	}
	if (!cv && m->m_flags & PMUTEX_FLAG_PRIVATE)
		THR_CRITICAL_LEAVE(curthread);
	return (error);
}

int
_pthread_mutex_getprioceiling(const pthread_mutex_t * __restrict mutex,
    int * __restrict prioceiling)
{
	struct pthread_mutex *m;

	if (*mutex == THR_PSHARED_PTR) {
		m = __thr_pshared_offpage(__DECONST(void *, mutex), 0);
		if (m == NULL)
			return (EINVAL);
		shared_mutex_init(m, NULL);
	} else {
		m = *mutex;
		if (m <= THR_MUTEX_DESTROYED)
			return (EINVAL);
	}
	if ((m->m_lock.m_flags & UMUTEX_PRIO_PROTECT) == 0)
		return (EINVAL);
	*prioceiling = m->m_lock.m_ceilings[0];
	return (0);
}

int
_pthread_mutex_setprioceiling(pthread_mutex_t * __restrict mutex,
    int ceiling, int * __restrict old_ceiling)
{
	struct pthread *curthread;
	struct pthread_mutex *m, *m1, *m2;
	struct mutex_queue *q, *qp;
	int qidx, ret;

	if (*mutex == THR_PSHARED_PTR) {
		m = __thr_pshared_offpage(mutex, 0);
		if (m == NULL)
			return (EINVAL);
		shared_mutex_init(m, NULL);
	} else {
		m = *mutex;
		if (m <= THR_MUTEX_DESTROYED)
			return (EINVAL);
	}
	if ((m->m_lock.m_flags & UMUTEX_PRIO_PROTECT) == 0)
		return (EINVAL);

	ret = __thr_umutex_set_ceiling(&m->m_lock, ceiling, old_ceiling);
	if (ret != 0)
		return (ret);

	curthread = _get_curthread();
	if (PMUTEX_OWNER_ID(m) == TID(curthread)) {
		mutex_assert_is_owned(m);
		m1 = TAILQ_PREV(m, mutex_queue, m_qe);
		m2 = TAILQ_NEXT(m, m_qe);
		if ((m1 != NULL && m1->m_lock.m_ceilings[0] > (u_int)ceiling) ||
		    (m2 != NULL && m2->m_lock.m_ceilings[0] < (u_int)ceiling)) {
			qidx = mutex_qidx(m);
			q = &curthread->mq[qidx];
			qp = &curthread->mq[qidx + 1];
			TAILQ_REMOVE(q, m, m_qe);
			if (!is_pshared_mutex(m))
				TAILQ_REMOVE(qp, m, m_pqe);
			TAILQ_FOREACH(m2, q, m_qe) {
				if (m2->m_lock.m_ceilings[0] > (u_int)ceiling) {
					TAILQ_INSERT_BEFORE(m2, m, m_qe);
					if (!is_pshared_mutex(m)) {
						while (m2 != NULL &&
						    is_pshared_mutex(m2)) {
							m2 = TAILQ_PREV(m2,
							    mutex_queue, m_qe);
						}
						if (m2 == NULL) {
							TAILQ_INSERT_HEAD(qp,
							    m, m_pqe);
						} else {
							TAILQ_INSERT_BEFORE(m2,
							    m, m_pqe);
						}
					}
					return (0);
				}
			}
			TAILQ_INSERT_TAIL(q, m, m_qe);
			if (!is_pshared_mutex(m))
				TAILQ_INSERT_TAIL(qp, m, m_pqe);
		}
	}
	return (0);
}

int
_pthread_mutex_getspinloops_np(pthread_mutex_t *mutex, int *count)
{
	struct pthread_mutex *m;
	int ret;

	ret = check_and_init_mutex(mutex, &m);
	if (ret == 0)
		*count = m->m_spinloops;
	return (ret);
}

int
__pthread_mutex_setspinloops_np(pthread_mutex_t *mutex, int count)
{
	struct pthread_mutex *m;
	int ret;

	ret = check_and_init_mutex(mutex, &m);
	if (ret == 0)
		m->m_spinloops = count;
	return (ret);
}

int
_pthread_mutex_getyieldloops_np(pthread_mutex_t *mutex, int *count)
{
	struct pthread_mutex *m;
	int ret;

	ret = check_and_init_mutex(mutex, &m);
	if (ret == 0)
		*count = m->m_yieldloops;
	return (ret);
}

int
__pthread_mutex_setyieldloops_np(pthread_mutex_t *mutex, int count)
{
	struct pthread_mutex *m;
	int ret;

	ret = check_and_init_mutex(mutex, &m);
	if (ret == 0)
		m->m_yieldloops = count;
	return (0);
}

int
_pthread_mutex_isowned_np(pthread_mutex_t *mutex)
{
	struct pthread_mutex *m;

	if (*mutex == THR_PSHARED_PTR) {
		m = __thr_pshared_offpage(mutex, 0);
		if (m == NULL)
			return (0);
		shared_mutex_init(m, NULL);
	} else {
		m = *mutex;
		if (m <= THR_MUTEX_DESTROYED)
			return (0);
	}
	return (PMUTEX_OWNER_ID(m) == TID(_get_curthread()));
}

int
_mutex_owned(struct pthread *curthread, const struct pthread_mutex *mp)
{

	if (__predict_false(mp <= THR_MUTEX_DESTROYED)) {
		if (mp == THR_MUTEX_DESTROYED)
			return (EINVAL);
		return (EPERM);
	}
	if (PMUTEX_OWNER_ID(mp) != TID(curthread))
		return (EPERM);
	return (0);                  
}

int
_pthread_mutex_consistent(pthread_mutex_t *mutex)
{
	struct pthread_mutex *m;
	struct pthread *curthread;

	if (*mutex == THR_PSHARED_PTR) {
		m = __thr_pshared_offpage(mutex, 0);
		if (m == NULL)
			return (EINVAL);
		shared_mutex_init(m, NULL);
	} else {
		m = *mutex;
		if (m <= THR_MUTEX_DESTROYED)
			return (EINVAL);
	}
	curthread = _get_curthread();
	if ((m->m_lock.m_flags & (UMUTEX_ROBUST | UMUTEX_NONCONSISTENT)) !=
	    (UMUTEX_ROBUST | UMUTEX_NONCONSISTENT))
		return (EINVAL);
	if (PMUTEX_OWNER_ID(m) != TID(curthread))
		return (EPERM);
	m->m_lock.m_flags &= ~UMUTEX_NONCONSISTENT;
	return (0);
}
