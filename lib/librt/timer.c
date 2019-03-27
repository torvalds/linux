/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2006 David Xu <davidxu@freebsd.org>
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

#include "namespace.h"
#include <errno.h>
#include <stddef.h>
#include <signal.h>
#include <stdlib.h>
#include <time.h>
#include "sigev_thread.h"
#include "un-namespace.h"

extern int __sys_ktimer_create(clockid_t, struct sigevent *__restrict,
	int *__restrict);
extern int __sys_ktimer_delete(int);
extern int __sys_ktimer_gettime(int, struct itimerspec *);
extern int __sys_ktimer_getoverrun(int);
extern int __sys_ktimer_settime(int, int,
	const struct itimerspec *__restrict, struct itimerspec *__restrict);

struct __timer {
	int oshandle;
	struct sigev_node *node;
};

__weak_reference(__timer_create, timer_create);
__weak_reference(__timer_create, _timer_create);
__weak_reference(__timer_delete, timer_delete);
__weak_reference(__timer_delete, _timer_delete);
__weak_reference(__timer_gettime, timer_gettime);
__weak_reference(__timer_gettime, _timer_gettime);
__weak_reference(__timer_settime, timer_settime);
__weak_reference(__timer_settime, _timer_settime);
__weak_reference(__timer_getoverrun, timer_getoverrun);
__weak_reference(__timer_getoverrun, _timer_getoverrun);

typedef void (*timer_func)(union sigval val, int overrun);

static void
timer_dispatch(struct sigev_node *sn)
{
	timer_func f = sn->sn_func;

	/* I want to avoid expired notification. */
	if (sn->sn_info.si_value.sival_int == sn->sn_gen)
		f(sn->sn_value, sn->sn_info.si_overrun);
}

int
__timer_create(clockid_t clockid, struct sigevent *evp, timer_t *timerid)
{
	struct __timer *timer;
	struct sigevent ev;
	struct sigev_node *sn;
	int ret, err;

	timer = malloc(sizeof(struct __timer));
	if (timer == NULL)
		return (-1);

	if (evp == NULL || evp->sigev_notify != SIGEV_THREAD) {
		ret = __sys_ktimer_create(clockid, evp, &timer->oshandle);
		if (ret == -1) {
			err = errno;
			free(timer);
			errno = err;
			return (ret);
		}
		timer->node = NULL;
		*timerid = timer;
		return (0);
	}

	if (__sigev_check_init()) {
		free(timer);
		errno = EINVAL;
		return (-1);
	}

	sn = __sigev_alloc(SI_TIMER, evp, NULL, 0);
	if (sn == NULL) {
		free(timer);
		errno = EAGAIN;
		return (-1);
	}

	__sigev_get_sigevent(sn, &ev, sn->sn_gen);
	ret = __sys_ktimer_create(clockid, &ev, &timer->oshandle);
	if (ret != 0) {
		err = errno;
		__sigev_free(sn);
		free(timer);
		errno = err;
		return (-1);
	}
	sn->sn_flags |= SNF_SYNC;
	sn->sn_dispatch = timer_dispatch;
	sn->sn_id = timer->oshandle;
	timer->node = sn;
	__sigev_list_lock();
	__sigev_register(sn);
	__sigev_list_unlock();
	*timerid = timer;
	return (0);
}

int
__timer_delete(timer_t timerid)
{
	int ret, err;

	if (timerid->node != NULL) {
		__sigev_list_lock();
		__sigev_delete_node(timerid->node);
		__sigev_list_unlock();
	}
	ret = __sys_ktimer_delete(timerid->oshandle);
	err = errno;
	free(timerid);
	errno = err;
	return (ret);	
}

int
__timer_gettime(timer_t timerid, struct itimerspec *value)
{

	return __sys_ktimer_gettime(timerid->oshandle, value);
}

int
__timer_getoverrun(timer_t timerid)
{

	return __sys_ktimer_getoverrun(timerid->oshandle);
}

int
__timer_settime(timer_t timerid, int flags,
	const struct itimerspec *__restrict value,
	struct itimerspec *__restrict ovalue)
{

	return __sys_ktimer_settime(timerid->oshandle,
		flags, value, ovalue);
}

#pragma weak timer_oshandle_np
int
timer_oshandle_np(timer_t timerid)
{

	return (timerid->oshandle);
}
