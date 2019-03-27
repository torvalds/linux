/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2005, David Xu <davidxu@freebsd.org>
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

#include "namespace.h"
#include <pthread.h>
#include "un-namespace.h"

#include "thr_private.h"

__weak_reference(_pthread_once, pthread_once);

#define ONCE_NEVER_DONE		PTHREAD_NEEDS_INIT
#define ONCE_DONE		PTHREAD_DONE_INIT
#define	ONCE_IN_PROGRESS	0x02
#define ONCE_WAIT		0x03

/*
 * POSIX:
 * The pthread_once() function is not a cancellation point. However,
 * if init_routine is a cancellation point and is canceled, the effect
 * on once_control shall be as if pthread_once() was never called.
 */
 
static void
once_cancel_handler(void *arg)
{
	pthread_once_t *once_control;

	once_control = arg;
	if (atomic_cmpset_rel_int(&once_control->state, ONCE_IN_PROGRESS,
	    ONCE_NEVER_DONE))
		return;
	atomic_store_rel_int(&once_control->state, ONCE_NEVER_DONE);
	_thr_umtx_wake(&once_control->state, INT_MAX, 0);
}

int
_pthread_once(pthread_once_t *once_control, void (*init_routine) (void))
{
	struct pthread *curthread;
	int state;

	_thr_check_init();

	for (;;) {
		state = once_control->state;
		if (state == ONCE_DONE) {
			atomic_thread_fence_acq();
			return (0);
		}
		if (state == ONCE_NEVER_DONE) {
			if (atomic_cmpset_int(&once_control->state, state,
			    ONCE_IN_PROGRESS))
				break;
		} else if (state == ONCE_IN_PROGRESS) {
			if (atomic_cmpset_int(&once_control->state, state,
			    ONCE_WAIT))
				_thr_umtx_wait_uint(&once_control->state,
				    ONCE_WAIT, NULL, 0);
		} else if (state == ONCE_WAIT) {
			_thr_umtx_wait_uint(&once_control->state, state,
			    NULL, 0);
		} else
			return (EINVAL);
        }

	curthread = _get_curthread();
	THR_CLEANUP_PUSH(curthread, once_cancel_handler, once_control);
	init_routine();
	THR_CLEANUP_POP(curthread, 0);
	if (atomic_cmpset_rel_int(&once_control->state, ONCE_IN_PROGRESS,
	    ONCE_DONE))
		return (0);
	atomic_store_rel_int(&once_control->state, ONCE_DONE);
	_thr_umtx_wake(&once_control->state, INT_MAX, 0);
	return (0);
}
