/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (C) 2010 David Xu <davidxu@freebsd.org>.
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

#include "namespace.h"
#include <sys/types.h>
#include <sys/queue.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <errno.h>
#include <machine/atomic.h>
#include <sys/umtx.h>
#include <limits.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <semaphore.h>
#include <unistd.h>
#include "un-namespace.h"
#include "libc_private.h"

__weak_reference(_sem_close, sem_close);
__weak_reference(_sem_destroy, sem_destroy);
__weak_reference(_sem_getvalue, sem_getvalue);
__weak_reference(_sem_init, sem_init);
__weak_reference(_sem_open, sem_open);
__weak_reference(_sem_post, sem_post);
__weak_reference(_sem_timedwait, sem_timedwait);
__weak_reference(_sem_clockwait_np, sem_clockwait_np);
__weak_reference(_sem_trywait, sem_trywait);
__weak_reference(_sem_unlink, sem_unlink);
__weak_reference(_sem_wait, sem_wait);

#define SEM_PREFIX	"/tmp/SEMD"
#define SEM_MAGIC	((u_int32_t)0x73656d32)

_Static_assert(SEM_VALUE_MAX <= USEM_MAX_COUNT, "SEM_VALUE_MAX too large");

struct sem_nameinfo {
	int open_count;
	char *name;
	dev_t dev;
	ino_t ino;
	sem_t *sem;
	LIST_ENTRY(sem_nameinfo) next;
};

static pthread_once_t once = PTHREAD_ONCE_INIT;
static pthread_mutex_t sem_llock;
static LIST_HEAD(, sem_nameinfo) sem_list = LIST_HEAD_INITIALIZER(sem_list);

static void
sem_prefork(void)
{
	
	_pthread_mutex_lock(&sem_llock);
}

static void
sem_postfork(void)
{

	_pthread_mutex_unlock(&sem_llock);
}

static void
sem_child_postfork(void)
{

	_pthread_mutex_unlock(&sem_llock);
}

static void
sem_module_init(void)
{

	_pthread_mutex_init(&sem_llock, NULL);
	_pthread_atfork(sem_prefork, sem_postfork, sem_child_postfork);
}

static inline int
sem_check_validity(sem_t *sem)
{

	if (sem->_magic == SEM_MAGIC)
		return (0);
	errno = EINVAL;
	return (-1);
}

int
_sem_init(sem_t *sem, int pshared, unsigned int value)
{

	if (value > SEM_VALUE_MAX) {
		errno = EINVAL;
		return (-1);
	}
 
	bzero(sem, sizeof(sem_t));
	sem->_magic = SEM_MAGIC;
	sem->_kern._count = (u_int32_t)value;
	sem->_kern._flags = pshared ? USYNC_PROCESS_SHARED : 0;
	return (0);
}

sem_t *
_sem_open(const char *name, int flags, ...)
{
	char path[PATH_MAX];
	struct stat sb;
	va_list ap;
	struct sem_nameinfo *ni;
	sem_t *sem, tmp;
	int errsave, fd, len, mode, value;

	ni = NULL;
	sem = NULL;
	fd = -1;
	value = 0;

	if (name[0] != '/') {
		errno = EINVAL;
		return (SEM_FAILED);
	}
	name++;
	strcpy(path, SEM_PREFIX);
	if (strlcat(path, name, sizeof(path)) >= sizeof(path)) {
		errno = ENAMETOOLONG;
		return (SEM_FAILED);
	}
	if (flags & ~(O_CREAT|O_EXCL)) {
		errno = EINVAL;
		return (SEM_FAILED);
	}
	if ((flags & O_CREAT) != 0) {
		va_start(ap, flags);
		mode = va_arg(ap, int);
		value = va_arg(ap, int);
		va_end(ap);
	}
	fd = -1;
	_pthread_once(&once, sem_module_init);

	_pthread_mutex_lock(&sem_llock);
	LIST_FOREACH(ni, &sem_list, next) {
		if (ni->name != NULL && strcmp(name, ni->name) == 0) {
			fd = _open(path, flags | O_RDWR | O_CLOEXEC |
			    O_EXLOCK, mode);
			if (fd == -1 || _fstat(fd, &sb) == -1) {
				ni = NULL;
				goto error;
			}
			if ((flags & (O_CREAT | O_EXCL)) == (O_CREAT |
			    O_EXCL) || ni->dev != sb.st_dev ||
			    ni->ino != sb.st_ino) {
				ni->name = NULL;
				ni = NULL;
				break;
			}
			ni->open_count++;
			sem = ni->sem;
			_pthread_mutex_unlock(&sem_llock);
			_close(fd);
			return (sem);
		}
	}

	len = sizeof(*ni) + strlen(name) + 1;
	ni = (struct sem_nameinfo *)malloc(len);
	if (ni == NULL) {
		errno = ENOSPC;
		goto error;
	}

	ni->name = (char *)(ni+1);
	strcpy(ni->name, name);

	if (fd == -1) {
		fd = _open(path, flags | O_RDWR | O_CLOEXEC | O_EXLOCK, mode);
		if (fd == -1 || _fstat(fd, &sb) == -1)
			goto error;
	}
	if (sb.st_size < sizeof(sem_t)) {
		tmp._magic = SEM_MAGIC;
		tmp._kern._count = value;
		tmp._kern._flags = USYNC_PROCESS_SHARED | SEM_NAMED;
		if (_write(fd, &tmp, sizeof(tmp)) != sizeof(tmp))
			goto error;
	}
	flock(fd, LOCK_UN);
	sem = mmap(NULL, sizeof(sem_t), PROT_READ | PROT_WRITE,
	    MAP_SHARED | MAP_NOSYNC, fd, 0);
	if (sem == MAP_FAILED) {
		sem = NULL;
		if (errno == ENOMEM)
			errno = ENOSPC;
		goto error;
	}
	if (sem->_magic != SEM_MAGIC) {
		errno = EINVAL;
		goto error;
	}
	ni->open_count = 1;
	ni->sem = sem;
	ni->dev = sb.st_dev;
	ni->ino = sb.st_ino;
	LIST_INSERT_HEAD(&sem_list, ni, next);
	_close(fd);
	_pthread_mutex_unlock(&sem_llock);
	return (sem);

error:
	errsave = errno;
	if (fd != -1)
		_close(fd);
	if (sem != NULL)
		munmap(sem, sizeof(sem_t));
	free(ni);
	_pthread_mutex_unlock(&sem_llock);
	errno = errsave;
	return (SEM_FAILED);
}

int
_sem_close(sem_t *sem)
{
	struct sem_nameinfo *ni;
	bool last;

	if (sem_check_validity(sem) != 0)
		return (-1);

	if (!(sem->_kern._flags & SEM_NAMED)) {
		errno = EINVAL;
		return (-1);
	}

	_pthread_once(&once, sem_module_init);

	_pthread_mutex_lock(&sem_llock);
	LIST_FOREACH(ni, &sem_list, next) {
		if (sem == ni->sem) {
			last = --ni->open_count == 0;
			if (last)
				LIST_REMOVE(ni, next);
			_pthread_mutex_unlock(&sem_llock);
			if (last) {
				munmap(sem, sizeof(*sem));
				free(ni);
			}
			return (0);
		}
	}
	_pthread_mutex_unlock(&sem_llock);
	errno = EINVAL;
	return (-1);
}

int
_sem_unlink(const char *name)
{
	char path[PATH_MAX];

	if (name[0] != '/') {
		errno = ENOENT;
		return -1;
	}
	name++;
	strcpy(path, SEM_PREFIX);
	if (strlcat(path, name, sizeof(path)) >= sizeof(path)) {
		errno = ENAMETOOLONG;
		return (-1);
	}

	return (unlink(path));
}

int
_sem_destroy(sem_t *sem)
{

	if (sem_check_validity(sem) != 0)
		return (-1);

	if (sem->_kern._flags & SEM_NAMED) {
		errno = EINVAL;
		return (-1);
	}
	sem->_magic = 0;
	return (0);
}

int
_sem_getvalue(sem_t * __restrict sem, int * __restrict sval)
{

	if (sem_check_validity(sem) != 0)
		return (-1);

	*sval = (int)USEM_COUNT(sem->_kern._count);
	return (0);
}

static __inline int
usem_wake(struct _usem2 *sem)
{

	return (_umtx_op(sem, UMTX_OP_SEM2_WAKE, 0, NULL, NULL));
}

static __inline int
usem_wait(struct _usem2 *sem, clockid_t clock_id, int flags,
    const struct timespec *rqtp, struct timespec *rmtp)
{
	struct {
		struct _umtx_time timeout;
		struct timespec remain;
	} tms;
	void *tm_p;
	size_t tm_size;
	int retval;

	if (rqtp == NULL) {
		tm_p = NULL;
		tm_size = 0;
	} else {
		tms.timeout._clockid = clock_id;
		tms.timeout._flags = (flags & TIMER_ABSTIME) ? UMTX_ABSTIME : 0;
		tms.timeout._timeout = *rqtp;
		tm_p = &tms;
		tm_size = sizeof(tms);
	}
	retval = _umtx_op(sem, UMTX_OP_SEM2_WAIT, 0, (void *)tm_size, tm_p);
	if (retval == -1 && errno == EINTR && (flags & TIMER_ABSTIME) == 0 &&
	    rqtp != NULL && rmtp != NULL) {
		*rmtp = tms.remain;
	}

	return (retval);
}

int
_sem_trywait(sem_t *sem)
{
	int val;

	if (sem_check_validity(sem) != 0)
		return (-1);

	while (USEM_COUNT(val = sem->_kern._count) > 0) {
		if (atomic_cmpset_acq_int(&sem->_kern._count, val, val - 1))
			return (0);
	}
	errno = EAGAIN;
	return (-1);
}

int
_sem_clockwait_np(sem_t * __restrict sem, clockid_t clock_id, int flags,
	const struct timespec *rqtp, struct timespec *rmtp)
{
	int val, retval;

	if (sem_check_validity(sem) != 0)
		return (-1);

	retval = 0;
	_pthread_testcancel();
	for (;;) {
		while (USEM_COUNT(val = sem->_kern._count) > 0) {
			if (atomic_cmpset_acq_int(&sem->_kern._count, val,
			    val - 1))
				return (0);
		}

		if (retval) {
			_pthread_testcancel();
			break;
		}

		/*
		 * The timeout argument is only supposed to
		 * be checked if the thread would have blocked.
		 */
		if (rqtp != NULL) {
			if (rqtp->tv_nsec >= 1000000000 || rqtp->tv_nsec < 0) {
				errno = EINVAL;
				return (-1);
			}
		}
		_pthread_cancel_enter(1);
		retval = usem_wait(&sem->_kern, clock_id, flags, rqtp, rmtp);
		_pthread_cancel_leave(0);
	}
	return (retval);
}

int
_sem_timedwait(sem_t * __restrict sem,
	const struct timespec * __restrict abstime)
{

	return (_sem_clockwait_np(sem, CLOCK_REALTIME, TIMER_ABSTIME, abstime,
	    NULL));
};

int
_sem_wait(sem_t *sem)
{

	return (_sem_timedwait(sem, NULL));
}

/*
 * POSIX:
 * The sem_post() interface is reentrant with respect to signals and may be
 * invoked from a signal-catching function. 
 * The implementation does not use lock, so it should be safe.
 */
int
_sem_post(sem_t *sem)
{
	unsigned int count;

	if (sem_check_validity(sem) != 0)
		return (-1);

	do {
		count = sem->_kern._count;
		if (USEM_COUNT(count) + 1 > SEM_VALUE_MAX) {
			errno = EOVERFLOW;
			return (-1);
		}
	} while (!atomic_cmpset_rel_int(&sem->_kern._count, count, count + 1));
	if (count & USEM_HAS_WAITERS)
		usem_wake(&sem->_kern);
	return (0);
}
