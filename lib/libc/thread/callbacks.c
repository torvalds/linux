/*
 * Copyright (c) 2014 Philip Guenther <guenther@openbsd.org>
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

#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "thread_private.h"
#include "rthread_cb.h"

static __dead void
_thread_canceled(void)
{
	pthread_exit(PTHREAD_CANCELED);
}

void
_thread_set_callbacks(const struct thread_callbacks *cb, size_t len)
{
	sigset_t allmask, omask;

	if (sizeof(*cb) != len) {
		fprintf(stderr, "library mismatch: libc expected %zu but"
		    " libpthread gave %zu\n", sizeof(*cb), len);
		fflush(stderr);
		_exit(44);
	}

	sigfillset(&allmask);
	if (sigprocmask(SIG_BLOCK, &allmask, &omask) == 0) {
		/* mprotect RW */
		memcpy(&_thread_cb, cb, sizeof(_thread_cb));

		/*
		 * These are supplied by libc, but only enabled
		 * here when we actually need to prep for doing MT.
		 */
		_thread_cb.tc_canceled		= _thread_canceled;
		_thread_cb.tc_malloc_lock	= _thread_malloc_lock;
		_thread_cb.tc_malloc_unlock	= _thread_malloc_unlock;
		_thread_cb.tc_atexit_lock	= _thread_atexit_lock;
		_thread_cb.tc_atexit_unlock	= _thread_atexit_unlock;
		_thread_cb.tc_atfork_lock	= _thread_atfork_lock;
		_thread_cb.tc_atfork_unlock	= _thread_atfork_unlock;
		_thread_cb.tc_arc4_lock		= _thread_arc4_lock;
		_thread_cb.tc_arc4_unlock	= _thread_arc4_unlock;
		_thread_cb.tc_mutex_lock	= _thread_mutex_lock;
		_thread_cb.tc_mutex_unlock	= _thread_mutex_unlock;
		_thread_cb.tc_mutex_destroy	= _thread_mutex_destroy;
		_thread_cb.tc_tag_lock		= _thread_tag_lock;
		_thread_cb.tc_tag_unlock	= _thread_tag_unlock;
		_thread_cb.tc_tag_storage	= _thread_tag_storage;

		/* mprotect RO | LOCKPERM | NOUNMAP */
		sigprocmask(SIG_SETMASK, &omask, NULL);
	}
}
