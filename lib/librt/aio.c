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
 *
 * $FreeBSD$
 *
 */

#include <sys/cdefs.h>
#include <sys/types.h>
#include <sys/syscall.h>
#include <sys/aio.h>

#include "namespace.h"
#include <errno.h>
#include <stddef.h>
#include <signal.h>
#include "sigev_thread.h"
#include "un-namespace.h"

__weak_reference(__aio_read, aio_read);
__weak_reference(__aio_write, aio_write);
__weak_reference(__aio_return, aio_return);
__weak_reference(__aio_waitcomplete, aio_waitcomplete);
__weak_reference(__aio_fsync, aio_fsync);
__weak_reference(__lio_listio, lio_listio);

typedef void (*aio_func)(union sigval val, struct aiocb *iocb);

extern int __sys_aio_read(struct aiocb *iocb);
extern int __sys_aio_write(struct aiocb *iocb);
extern ssize_t __sys_aio_waitcomplete(struct aiocb **iocbp, struct timespec *timeout);
extern ssize_t __sys_aio_return(struct aiocb *iocb);
extern int __sys_aio_error(struct aiocb *iocb);
extern int __sys_aio_fsync(int op, struct aiocb *iocb);
extern int __sys_lio_listio(int mode, struct aiocb * const list[], int nent,
    struct sigevent *sig);

static void
aio_dispatch(struct sigev_node *sn)
{
	aio_func f = sn->sn_func;

	f(sn->sn_value, (struct aiocb *)sn->sn_id);
}

static int
aio_sigev_alloc(sigev_id_t id, struct sigevent *sigevent,
    struct sigev_node **sn, struct sigevent *saved_ev)
{
	if (__sigev_check_init()) {
		/* This might be that thread library is not enabled. */
		errno = EINVAL;
		return (-1);
	}

	*sn = __sigev_alloc(SI_ASYNCIO, sigevent, NULL, 1);
	if (*sn == NULL) {
		errno = EAGAIN;
		return (-1);
	}
	
	*saved_ev = *sigevent;
	(*sn)->sn_id = id;
	__sigev_get_sigevent(*sn, sigevent, (*sn)->sn_id);
	(*sn)->sn_dispatch = aio_dispatch;

	__sigev_list_lock();
	__sigev_register(*sn);
	__sigev_list_unlock();

	return (0);
}

static int
aio_io(struct aiocb *iocb, int (*sysfunc)(struct aiocb *iocb))
{
	struct sigev_node *sn;
	struct sigevent saved_ev;
	int ret, err;

	if (iocb->aio_sigevent.sigev_notify != SIGEV_THREAD) {
		ret = sysfunc(iocb);
		return (ret);
	}

	ret = aio_sigev_alloc((sigev_id_t)iocb, &iocb->aio_sigevent, &sn,
			      &saved_ev);
	if (ret)
		return (ret);
	ret = sysfunc(iocb);
	iocb->aio_sigevent = saved_ev;
	if (ret != 0) {
		err = errno;
		__sigev_list_lock();
		__sigev_delete_node(sn);
		__sigev_list_unlock();
		errno = err;
	}
	return (ret);
}

int
__aio_read(struct aiocb *iocb)
{

	return aio_io(iocb, &__sys_aio_read);
}

int
__aio_write(struct aiocb *iocb)
{

	return aio_io(iocb, &__sys_aio_write);
}

ssize_t
__aio_waitcomplete(struct aiocb **iocbp, struct timespec *timeout)
{
	ssize_t ret;
	int err;

	ret = __sys_aio_waitcomplete(iocbp, timeout);
	if (*iocbp) {
		if ((*iocbp)->aio_sigevent.sigev_notify == SIGEV_THREAD) {
			err = errno;
			__sigev_list_lock();
			__sigev_delete(SI_ASYNCIO, (sigev_id_t)(*iocbp));
			__sigev_list_unlock();
			errno = err;
		}
	}

	return (ret);
}

ssize_t
__aio_return(struct aiocb *iocb)
{

	if (iocb->aio_sigevent.sigev_notify == SIGEV_THREAD) {
		if (__sys_aio_error(iocb) == EINPROGRESS) {
			/*
			 * Fail with EINVAL to match the semantics of
			 * __sys_aio_return() for an in-progress
			 * request.
			 */
			errno = EINVAL;
			return (-1);
		}
		__sigev_list_lock();
		__sigev_delete(SI_ASYNCIO, (sigev_id_t)iocb);
		__sigev_list_unlock();
	}

	return __sys_aio_return(iocb);
}

int
__aio_fsync(int op, struct aiocb *iocb)
{
	struct sigev_node *sn;
	struct sigevent saved_ev;
	int ret, err;

	if (iocb->aio_sigevent.sigev_notify != SIGEV_THREAD)
		return __sys_aio_fsync(op, iocb);

	ret = aio_sigev_alloc((sigev_id_t)iocb, &iocb->aio_sigevent, &sn,
			      &saved_ev);
	if (ret)
		return (ret);
	ret = __sys_aio_fsync(op, iocb);
	iocb->aio_sigevent = saved_ev;
	if (ret != 0) {
		err = errno;
		__sigev_list_lock();
		__sigev_delete_node(sn);
		__sigev_list_unlock();
		errno = err;
	}
	return (ret);
}

int
__lio_listio(int mode, struct aiocb * const list[], int nent,
    struct sigevent *sig)
{
	struct sigev_node *sn;
	struct sigevent saved_ev;
	int ret, err;

	if (sig == NULL || sig->sigev_notify != SIGEV_THREAD)
		return (__sys_lio_listio(mode, list, nent, sig));

	ret = aio_sigev_alloc((sigev_id_t)list, sig, &sn, &saved_ev);
	if (ret)
		return (ret);
	ret = __sys_lio_listio(mode, list, nent, sig);
	*sig = saved_ev;
	if (ret != 0) {
		err = errno;
		__sigev_list_lock();
		__sigev_delete_node(sn);
		__sigev_list_unlock();
		errno = err;
	}
	return (ret);
}
