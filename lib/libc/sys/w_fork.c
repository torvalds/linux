/*	$OpenBSD: w_fork.c,v 1.3 2016/05/07 19:05:22 guenther Exp $ */

/*
 * Copyright (c) 2008 Kurt Miller <kurt@openbsd.org>
 * Copyright (c) 2008 Philip Guenther <guenther@openbsd.org>
 * Copyright (c) 2003 Daniel Eischen <deischen@freebsd.org>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Neither the name of the author nor the names of any co-contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
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
 * $FreeBSD: /repoman/r/ncvs/src/lib/libc_r/uthread/uthread_atfork.c,v 1.1 2004/12/10 03:36:45 grog Exp $
 */

#include <tib.h>
#include <unistd.h>
#include "thread_private.h"
#include "atfork.h"

pid_t
WRAP(fork)(void)
{
	struct atfork_fn *p;
	pid_t newid;

	/*
	 * In the common case the list is empty; remain async-signal-safe
	 * then by skipping the locking and just forking
	 */
	if (TAILQ_FIRST(&_atfork_list) == NULL) {
		if (_thread_cb.tc_fork != NULL)
			return _thread_cb.tc_fork();
		newid = fork();
		if (newid == 0)
			TIB_GET()->tib_tid = getthrid();
		return newid;
	}

	_ATFORK_LOCK();
	TAILQ_FOREACH_REVERSE(p, &_atfork_list, atfork_listhead, fn_next)
		if (p->fn_prepare)
			p->fn_prepare();

	if (_thread_cb.tc_fork != NULL)
		newid = _thread_cb.tc_fork();
	else {
		newid = fork();
		if (newid == 0)
			TIB_GET()->tib_tid = getthrid();
	}

	if (newid == 0) {
		TAILQ_FOREACH(p, &_atfork_list, fn_next)
			if (p->fn_child)
				p->fn_child();
	} else {
		TAILQ_FOREACH(p, &_atfork_list, fn_next)
			if (p->fn_parent)
				p->fn_parent();
	}
	_ATFORK_UNLOCK();

	return (newid);
}
DEF_WRAP(fork);
