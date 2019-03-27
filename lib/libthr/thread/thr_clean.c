/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *

 * Copyright (c) 1995 John Birrell <jb@cimlogic.com.au>.
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
 * 3. Neither the name of the author nor the names of any co-contributors
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
#include <signal.h>
#include <errno.h>
#include <stdlib.h>
#include <pthread.h>
#include "un-namespace.h"

#include "thr_private.h"

#undef pthread_cleanup_push
#undef pthread_cleanup_pop

/* old binary compatible interfaces */
__weak_reference(_pthread_cleanup_push, pthread_cleanup_push);
__weak_reference(_pthread_cleanup_pop, pthread_cleanup_pop);

void
__pthread_cleanup_push_imp(void (*routine)(void *), void *arg,
	struct _pthread_cleanup_info *info)
{
	struct pthread	*curthread = _get_curthread();
	struct pthread_cleanup *newbuf;

	newbuf = (void *)info;
	newbuf->routine = routine;
	newbuf->routine_arg = arg;
	newbuf->onheap = 0;
	newbuf->prev = curthread->cleanup;
	curthread->cleanup = newbuf;
}

void
__pthread_cleanup_pop_imp(int execute)
{
	struct pthread	*curthread = _get_curthread();
	struct pthread_cleanup *old;

	if ((old = curthread->cleanup) != NULL) {
		curthread->cleanup = old->prev;
		if (execute)
			old->routine(old->routine_arg);
		if (old->onheap)
			free(old);
	}
}

void
_pthread_cleanup_push(void (*routine) (void *), void *arg)
{
	struct pthread	*curthread = _get_curthread();
	struct pthread_cleanup *newbuf;
#ifdef _PTHREAD_FORCED_UNWIND
	curthread->unwind_disabled = 1;
#endif
	if ((newbuf = (struct pthread_cleanup *)
	    malloc(sizeof(struct pthread_cleanup))) != NULL) {
		newbuf->routine = routine;
		newbuf->routine_arg = arg;
		newbuf->onheap = 1;
		newbuf->prev = curthread->cleanup;
		curthread->cleanup = newbuf;
	}
}

void
_pthread_cleanup_pop(int execute)
{
	__pthread_cleanup_pop_imp(execute);
}
