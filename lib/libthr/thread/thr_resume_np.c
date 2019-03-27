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
#include <errno.h>
#include <pthread.h>
#include <pthread_np.h>
#include "un-namespace.h"

#include "thr_private.h"

__weak_reference(_pthread_resume_np, pthread_resume_np);
__weak_reference(_pthread_resume_all_np, pthread_resume_all_np);

static void resume_common(struct pthread *thread);

/* Resume a thread: */
int
_pthread_resume_np(pthread_t thread)
{
	struct pthread *curthread = _get_curthread();
	int ret;

	/* Add a reference to the thread: */
	if ((ret = _thr_find_thread(curthread, thread, /*include dead*/0)) == 0) {
		/* Lock the threads scheduling queue: */
		resume_common(thread);
		THR_THREAD_UNLOCK(curthread, thread);
	}
	return (ret);
}

void
_pthread_resume_all_np(void)
{
	struct pthread *curthread = _get_curthread();
	struct pthread *thread;
	int old_nocancel;

	old_nocancel = curthread->no_cancel;
	curthread->no_cancel = 1;
	_thr_suspend_all_lock(curthread);
	/* Take the thread list lock: */
	THREAD_LIST_RDLOCK(curthread);

	TAILQ_FOREACH(thread, &_thread_list, tle) {
		if (thread != curthread) {
			THR_THREAD_LOCK(curthread, thread);
			resume_common(thread);
			THR_THREAD_UNLOCK(curthread, thread);
		}
	}

	/* Release the thread list lock: */
	THREAD_LIST_UNLOCK(curthread);
	_thr_suspend_all_unlock(curthread);
	curthread->no_cancel = old_nocancel;
	_thr_testcancel(curthread);
}

static void
resume_common(struct pthread *thread)
{
	/* Clear the suspend flag: */
	thread->flags &= ~(THR_FLAGS_NEED_SUSPEND | THR_FLAGS_SUSPENDED);
	thread->cycle++;
	_thr_umtx_wake(&thread->cycle, 1, 0);
}
