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

#include <pthread.h>
#include <stdint.h>
#include <stdlib.h>

#include "threads.h"

struct thrd_param {
	thrd_start_t	 func;
	void		*arg;
};

static void *
thrd_entry(void *arg)
{
	struct thrd_param tp;

	tp = *(struct thrd_param *)arg;
	free(arg);
	return ((void *)(intptr_t)tp.func(tp.arg));
}

int
thrd_create(thrd_t *thr, thrd_start_t func, void *arg)
{
	struct thrd_param *tp;

	/*
	 * Work around return type inconsistency.  Wrap execution using
	 * a function conforming to pthread_create()'s start_routine.
	 */
	tp = malloc(sizeof(*tp));
	if (tp == NULL)
		return (thrd_nomem);
	tp->func = func;
	tp->arg = arg;
	if (pthread_create(thr, NULL, thrd_entry, tp) != 0) {
		free(tp);
		return (thrd_error);
	}
	return (thrd_success);
}

thrd_t
thrd_current(void)
{

	return (pthread_self());
}

int
thrd_detach(thrd_t thr)
{

	if (pthread_detach(thr) != 0)
		return (thrd_error);
	return (thrd_success);
}

int
thrd_equal(thrd_t thr0, thrd_t thr1)
{

	return (pthread_equal(thr0, thr1));
}

_Noreturn void
thrd_exit(int res)
{

	pthread_exit((void *)(intptr_t)res);
}

int
thrd_join(thrd_t thr, int *res)
{
	void *value_ptr;

	if (pthread_join(thr, &value_ptr) != 0)
		return (thrd_error);
	if (res != NULL)
		*res = (intptr_t)value_ptr;
	return (thrd_success);
}

int
thrd_sleep(const struct timespec *duration, struct timespec *remaining)
{

	return (nanosleep(duration, remaining));
}

void
thrd_yield(void)
{

	pthread_yield();
}
