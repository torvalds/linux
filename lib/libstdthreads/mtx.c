/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2011 Ed Schouten <ed@FreeBSD.org>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
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
 *
 * $FreeBSD$
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <errno.h>
#include <pthread.h>

#include "threads.h"

void
mtx_destroy(mtx_t *mtx)
{

	(void)pthread_mutex_destroy(mtx);
}

int
mtx_init(mtx_t *mtx, int type)
{
	pthread_mutexattr_t attr;
	int mt;

	switch (type) {
	case mtx_plain:
	case mtx_timed:
		mt = PTHREAD_MUTEX_NORMAL;
		break;
	case mtx_plain | mtx_recursive:
	case mtx_timed | mtx_recursive:
		mt = PTHREAD_MUTEX_RECURSIVE;
		break;
	default:
		return (thrd_error);
	}

	if (pthread_mutexattr_init(&attr) != 0)
		return (thrd_error);
	if (pthread_mutexattr_settype(&attr, mt) != 0)
		return (thrd_error);
	if (pthread_mutex_init(mtx, &attr) != 0)
		return (thrd_error);
	return (thrd_success);
}

int
mtx_lock(mtx_t *mtx)
{

	if (pthread_mutex_lock(mtx) != 0)
		return (thrd_error);
	return (thrd_success);
}

int
mtx_timedlock(mtx_t *restrict mtx, const struct timespec *restrict ts)
{

	switch (pthread_mutex_timedlock(mtx, ts)) {
	case 0:
		return (thrd_success);
	case ETIMEDOUT:
		return (thrd_timedout);
	default:
		return (thrd_error);
	}
}

int
mtx_trylock(mtx_t *mtx)
{

	switch (pthread_mutex_trylock(mtx)) {
	case 0:
		return (thrd_success);
	case EBUSY:
		return (thrd_busy);
	default:
		return (thrd_error);
	}
}

int
mtx_unlock(mtx_t *mtx)
{

	if (pthread_mutex_unlock(mtx) != 0)
		return (thrd_error);
	return (thrd_success);
}
