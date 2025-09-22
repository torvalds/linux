/*	$OpenBSD: atfork.c,v 1.4 2022/12/27 17:10:06 jmc Exp $ */

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

#include <errno.h>
#include <pthread.h>
#include <stdlib.h>

#include "thread_private.h"
#include "atfork.h"

int	_thread_atfork(void (*_prepare)(void), void (*_parent)(void),
	    void (*_child)(void), void *_dso);
PROTO_NORMAL(_thread_atfork);

int
_thread_atfork(void (*prepare)(void), void (*parent)(void),
    void (*child)(void), void *dso)
{
	struct atfork_fn *af;

	if ((af = malloc(sizeof *af)) == NULL)
		return (ENOMEM);

	af->fn_prepare = prepare;
	af->fn_parent = parent;
	af->fn_child = child;
	af->fn_dso = dso;
	_ATFORK_LOCK();
	TAILQ_INSERT_TAIL(&_atfork_list, af, fn_next);
	_ATFORK_UNLOCK();
	return (0);
}
DEF_STRONG(_thread_atfork);

/*
 * Copy of pthread_atfork() used by libc and anything statically linked
 * into the executable.  This passes NULL for the dso, so the callbacks
 * are never removed by dlclose()
 */
int
pthread_atfork(void (*prep)(void), void (*parent)(void), void (*child)(void))
{
	return (_thread_atfork(prep, parent, child, NULL));
}
