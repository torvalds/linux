/*	$OpenBSD: rthread_np.c,v 1.24 2023/01/07 05:24:58 guenther Exp $	*/
/*
 * Copyright (c) 2004,2005 Ted Unangst <tedu@openbsd.org>
 * Copyright (c) 2005 Otto Moerbeek <otto@openbsd.org>
 * All Rights Reserved.
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include <sys/types.h>
#include <sys/time.h>
#include <sys/lock.h>
#include <sys/resource.h>
#include <sys/queue.h>
#include <sys/sysctl.h>

#include <errno.h>
#include <pthread.h>
#include <pthread_np.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <tib.h>
#include <unistd.h>

#include "rthread.h"

REDIRECT_SYSCALL(sysctl);
REDIRECT_SYSCALL(getthrname);
REDIRECT_SYSCALL(setthrname);

void
pthread_set_name_np(pthread_t thread, const char *name)
{
	pid_t tid = 0;

	if (thread != pthread_self())
		tid = thread->tib->tib_tid;
	setthrname(tid, name);
}

void
pthread_get_name_np(pthread_t thread, char *name, size_t len)
{
	pid_t tid = 0;

	if (thread != pthread_self())
		tid = thread->tib->tib_tid;
	getthrname(tid, name, len);
}

int
pthread_main_np(void)
{
	return (!_threads_ready ||
	    (TIB_GET()->tib_thread_flags & TIB_THREAD_INITIAL_STACK) ? 1 : 0);
}


/*
 * Return stack info from the given thread.  Based upon the solaris
 * thr_stksegment function.  Note that the returned ss_sp member is the
 * *top* of the allocated stack area, unlike in sigaltstack() where
 * it's the bottom.  You'll have to ask Sun what they were thinking...
 *
 * This function taken from the uthread library, with the following
 * license: 
 * PUBLIC DOMAIN: No Rights Reserved. Marco S Hyman <marc@snafu.org> */
int
pthread_stackseg_np(pthread_t thread, stack_t *sinfo)
{
	if (thread->stack) {
#ifdef MACHINE_STACK_GROWS_UP
		sinfo->ss_sp = thread->stack->base;
#else
		sinfo->ss_sp = (char *)thread->stack->base +
		    thread->stack->len;
#endif
		sinfo->ss_size = thread->stack->len;
		if (thread->stack->guardsize != 1)
			sinfo->ss_size -= thread->stack->guardsize;
		sinfo->ss_flags = 0;
		return (0);
	} else if (thread->tib->tib_thread_flags & TIB_THREAD_INITIAL_STACK) {
		static struct _ps_strings _ps;
		static struct rlimit rl;
		static int gotself;

		if (!_threads_ready)		/* for ROUND_TO_PAGE */
			_rthread_init();

		if (gotself == 0) {
			const int mib[2] = { CTL_VM, VM_PSSTRINGS };
			size_t len;

			if (getrlimit(RLIMIT_STACK, &rl) != 0)
				return (EAGAIN);

			len = sizeof(_ps);
			if (sysctl(mib, 2, &_ps, &len, NULL, 0) != 0)
				return (EAGAIN);
			gotself = 1;
		}

		/*
		 * Provides a rough estimation of stack bounds.   Caller
		 * likely wants to know for the purpose of inspecting call
		 * frames, but VM_PSSTRINGS points to process arguments...
		 */
#ifdef MACHINE_STACK_GROWS_UP
		sinfo->ss_sp = _ps.val;
#else
		sinfo->ss_sp = (void *)ROUND_TO_PAGE((uintptr_t)_ps.val);
#endif
		sinfo->ss_size = (size_t)rl.rlim_cur;
		sinfo->ss_flags = 0;
		return (0);
	}
	return (EAGAIN);
}
