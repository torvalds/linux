/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (C) 2010 David Xu <davidxu@freebsd.org>.
 * Copyright (C) 2000 Jason Evans <jasone@freebsd.org>.
 * All rights reserved.
 * 
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice(s), this list of conditions and the following disclaimer as
 *    the first lines of this file unmodified other than the possible
 *    addition of one or more copyright notices.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice(s), this list of conditions and the following disclaimer in
 *    the documentation and/or other materials provided with the
 *    distribution.
 * 
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDER(S) ``AS IS'' AND ANY
 * EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE COPYRIGHT HOLDER(S) BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR
 * BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE
 * OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE,
 * EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * $FreeBSD$
 */

/*
 * Some notes about this implementation.
 *
 * This is mostly a simple implementation of POSIX semaphores that
 * does not need threading.  Any semaphore created is a kernel-based
 * semaphore regardless of the pshared attribute.  This is necessary
 * because libc's stub for pthread_cond_wait() doesn't really wait,
 * and it is not worth the effort impose this behavior on libc.
 *
 * All functions here are designed to be thread-safe so that a
 * threads library need not provide wrappers except to make
 * sem_wait() and sem_timedwait() cancellation points or to
 * provide a faster userland implementation for non-pshared
 * semaphores.
 *
 * Also, this implementation of semaphores cannot really support
 * real pshared semaphores.  The sem_t is an allocated object
 * and can't be seen by other processes when placed in shared
 * memory.  It should work across forks as long as the semaphore
 * is created before any forks.
 *
 * The function sem_init() should be overridden by a threads
 * library if it wants to provide a different userland version
 * of semaphores.  The functions sem_wait() and sem_timedwait()
 * need to be wrapped to provide cancellation points.  The function
 * sem_post() may need to be wrapped to be signal-safe.
 */
#include "namespace.h"
#include <sys/types.h>
#include <sys/queue.h>
#include <machine/atomic.h>
#include <errno.h>
#include <sys/umtx.h>
#include <sys/_semaphore.h>
#include <limits.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdarg.h>
#include <stdlib.h>
#include <time.h>
#include "un-namespace.h"
#include "libc_private.h"

/*
 * Old semaphore definitions.
 */
struct sem {
#define SEM_MAGIC       ((u_int32_t) 0x09fa4012)
        u_int32_t       magic;
        pthread_mutex_t lock;
        pthread_cond_t  gtzero;
        u_int32_t       count;
        u_int32_t       nwaiters;
#define SEM_USER        (NULL)
        semid_t         semid;  /* semaphore id if kernel (shared) semaphore */
        int             syssem; /* 1 if kernel (shared) semaphore */
        LIST_ENTRY(sem) entry;
        struct sem      **backpointer;
};

typedef struct sem* sem_t;

#define SEM_FAILED     ((sem_t *)0)
#define SEM_VALUE_MAX  __INT_MAX

#define SYM_FB10(sym)                   __CONCAT(sym, _fb10)
#define WEAK_REF(sym, alias)            __weak_reference(sym, alias)
#define SYM_COMPAT(sym, impl, ver)      __sym_compat(sym, impl, ver)
 
#define FB10_COMPAT(func, sym)                          \
        WEAK_REF(func, SYM_FB10(sym));                  \
        SYM_COMPAT(sym, SYM_FB10(sym), FBSD_1.0)

static sem_t sem_alloc(unsigned int value, semid_t semid, int system_sem);
static void  sem_free(sem_t sem);

static LIST_HEAD(, sem) named_sems = LIST_HEAD_INITIALIZER(named_sems);
static pthread_mutex_t named_sems_mtx = PTHREAD_MUTEX_INITIALIZER;

FB10_COMPAT(_libc_sem_init_compat, sem_init);
FB10_COMPAT(_libc_sem_destroy_compat, sem_destroy);
FB10_COMPAT(_libc_sem_open_compat, sem_open);
FB10_COMPAT(_libc_sem_close_compat, sem_close);
FB10_COMPAT(_libc_sem_unlink_compat, sem_unlink);
FB10_COMPAT(_libc_sem_wait_compat, sem_wait);
FB10_COMPAT(_libc_sem_trywait_compat, sem_trywait);
FB10_COMPAT(_libc_sem_timedwait_compat, sem_timedwait);
FB10_COMPAT(_libc_sem_post_compat, sem_post);
FB10_COMPAT(_libc_sem_getvalue_compat, sem_getvalue);

static inline int
sem_check_validity(sem_t *sem)
{

	if ((sem != NULL) && ((*sem)->magic == SEM_MAGIC))
		return (0);
	else {
		errno = EINVAL;
		return (-1);
	}
}

static void
sem_free(sem_t sem)
{

	sem->magic = 0;
	free(sem);
}

static sem_t
sem_alloc(unsigned int value, semid_t semid, int system_sem)
{
	sem_t sem;

	if (value > SEM_VALUE_MAX) {
		errno = EINVAL;
		return (NULL);
	}

	sem = (sem_t)malloc(sizeof(struct sem));
	if (sem == NULL) {
		errno = ENOSPC;
		return (NULL);
	}

	sem->count = (u_int32_t)value;
	sem->nwaiters = 0;
	sem->magic = SEM_MAGIC;
	sem->semid = semid;
	sem->syssem = system_sem;
	return (sem);
}

int
_libc_sem_init_compat(sem_t *sem, int pshared, unsigned int value)
{
	semid_t semid;

	/*
	 * We always have to create the kernel semaphore if the
	 * threads library isn't present since libc's version of
	 * pthread_cond_wait() is just a stub that doesn't really
	 * wait.
	 */
	semid = (semid_t)SEM_USER;
	if ((pshared != 0) && ksem_init(&semid, value) != 0)
		return (-1);

	*sem = sem_alloc(value, semid, pshared);
	if ((*sem) == NULL) {
		if (pshared != 0)
			ksem_destroy(semid);
		return (-1);
	}
	return (0);
}

int
_libc_sem_destroy_compat(sem_t *sem)
{
	int retval;

	if (sem_check_validity(sem) != 0)
		return (-1);

	/*
	 * If this is a system semaphore let the kernel track it otherwise
	 * make sure there are no waiters.
	 */
	if ((*sem)->syssem != 0)
		retval = ksem_destroy((*sem)->semid);
	else if ((*sem)->nwaiters > 0) {
		errno = EBUSY;
		retval = -1;
	}
	else {
		retval = 0;
		(*sem)->magic = 0;
	}

	if (retval == 0)
		sem_free(*sem);
	return (retval);
}

sem_t *
_libc_sem_open_compat(const char *name, int oflag, ...)
{
	sem_t *sem;
	sem_t s;
	semid_t semid;
	mode_t mode;
	unsigned int value;

	mode = 0;
	value = 0;

	if ((oflag & O_CREAT) != 0) {
		va_list ap;

		va_start(ap, oflag);
		mode = va_arg(ap, int);
		value = va_arg(ap, unsigned int);
		va_end(ap);
	}
	/*
	 * we can be lazy and let the kernel handle the "oflag",
	 * we'll just merge duplicate IDs into our list.
	 */
	if (ksem_open(&semid, name, oflag, mode, value) == -1)
		return (SEM_FAILED);
	/*
	 * search for a duplicate ID, we must return the same sem_t *
	 * if we locate one.
	 */
	_pthread_mutex_lock(&named_sems_mtx);
	LIST_FOREACH(s, &named_sems, entry) {
		if (s->semid == semid) {
			sem = s->backpointer;
			_pthread_mutex_unlock(&named_sems_mtx);
			return (sem);
		}
	}
	sem = (sem_t *)malloc(sizeof(*sem));
	if (sem == NULL)
		goto err;
	*sem = sem_alloc(value, semid, 1);
	if ((*sem) == NULL)
		goto err;
	LIST_INSERT_HEAD(&named_sems, *sem, entry);
	(*sem)->backpointer = sem;
	_pthread_mutex_unlock(&named_sems_mtx);
	return (sem);
err:
	_pthread_mutex_unlock(&named_sems_mtx);
	ksem_close(semid);
	if (sem != NULL) {
		if (*sem != NULL)
			sem_free(*sem);
		else
			errno = ENOSPC;
		free(sem);
	} else {
		errno = ENOSPC;
	}
	return (SEM_FAILED);
}

int
_libc_sem_close_compat(sem_t *sem)
{

	if (sem_check_validity(sem) != 0)
		return (-1);

	if ((*sem)->syssem == 0) {
		errno = EINVAL;
		return (-1);
	}

	_pthread_mutex_lock(&named_sems_mtx);
	if (ksem_close((*sem)->semid) != 0) {
		_pthread_mutex_unlock(&named_sems_mtx);
		return (-1);
	}
	LIST_REMOVE((*sem), entry);
	_pthread_mutex_unlock(&named_sems_mtx);
	sem_free(*sem);
	*sem = NULL;
	free(sem);
	return (0);
}

int
_libc_sem_unlink_compat(const char *name)
{

	return (ksem_unlink(name));
}

static int
_umtx_wait_uint(volatile unsigned *mtx, unsigned id, const struct timespec *abstime)
{
	struct _umtx_time *tm_p, timeout;
	size_t tm_size;

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
	return _umtx_op(__DEVOLATILE(void *, mtx),
		UMTX_OP_WAIT_UINT_PRIVATE, id, 
		(void *)tm_size, __DECONST(void*, tm_p));
}

static int
_umtx_wake(volatile void *mtx)
{
	return _umtx_op(__DEVOLATILE(void *, mtx), UMTX_OP_WAKE_PRIVATE,
			1, NULL, NULL);
}

#define TIMESPEC_SUB(dst, src, val)                             \
        do {                                                    \
                (dst)->tv_sec = (src)->tv_sec - (val)->tv_sec;  \
                (dst)->tv_nsec = (src)->tv_nsec - (val)->tv_nsec; \
                if ((dst)->tv_nsec < 0) {                       \
                        (dst)->tv_sec--;                        \
                        (dst)->tv_nsec += 1000000000;           \
                }                                               \
        } while (0)


static void
sem_cancel_handler(void *arg)
{
	sem_t *sem = arg;

	atomic_add_int(&(*sem)->nwaiters, -1);
	if ((*sem)->nwaiters && (*sem)->count)
		_umtx_wake(&(*sem)->count);
}

int
_libc_sem_timedwait_compat(sem_t * __restrict sem,
	const struct timespec * __restrict abstime)
{
	int val, retval;

	if (sem_check_validity(sem) != 0)
		return (-1);

	if ((*sem)->syssem != 0) {
		_pthread_cancel_enter(1);
		retval = ksem_wait((*sem)->semid); /* XXX no timeout */
		_pthread_cancel_leave(retval == -1);
		return (retval);
	}

	retval = 0;
	_pthread_testcancel();
	for (;;) {
		while ((val = (*sem)->count) > 0) {
			if (atomic_cmpset_acq_int(&(*sem)->count, val, val - 1))
				return (0);
		}
		if (retval) {
			_pthread_testcancel();
			break;
		}
		if (abstime) {
			if (abstime->tv_nsec >= 1000000000 || abstime->tv_nsec < 0) {
				errno = EINVAL;
				return (-1);
			}
		}
		atomic_add_int(&(*sem)->nwaiters, 1);
		pthread_cleanup_push(sem_cancel_handler, sem);
		_pthread_cancel_enter(1);
		retval = _umtx_wait_uint(&(*sem)->count, 0, abstime);
		_pthread_cancel_leave(0);
		pthread_cleanup_pop(0);
		atomic_add_int(&(*sem)->nwaiters, -1);
	}
	return (retval);
}

int
_libc_sem_wait_compat(sem_t *sem)
{
	return _libc_sem_timedwait_compat(sem, NULL);
}

int
_libc_sem_trywait_compat(sem_t *sem)
{
	int val;

	if (sem_check_validity(sem) != 0)
		return (-1);

	if ((*sem)->syssem != 0)
 		return ksem_trywait((*sem)->semid);

	while ((val = (*sem)->count) > 0) {
		if (atomic_cmpset_acq_int(&(*sem)->count, val, val - 1))
			return (0);
	}
	errno = EAGAIN;
	return (-1);
}

int
_libc_sem_post_compat(sem_t *sem)
{

	if (sem_check_validity(sem) != 0)
		return (-1);

	if ((*sem)->syssem != 0)
		return ksem_post((*sem)->semid);

	atomic_add_rel_int(&(*sem)->count, 1);
	rmb();
	if ((*sem)->nwaiters)
		return _umtx_wake(&(*sem)->count);
	return (0);
}

int
_libc_sem_getvalue_compat(sem_t * __restrict sem, int * __restrict sval)
{
	int retval;

	if (sem_check_validity(sem) != 0)
		return (-1);

	if ((*sem)->syssem != 0)
		retval = ksem_getvalue((*sem)->semid, sval);
	else {
		*sval = (int)(*sem)->count;
		retval = 0;
	}
	return (retval);
}
