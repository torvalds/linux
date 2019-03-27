/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2009 Hudson River Trading LLC
 * Written by: John H. Baldwin <jhb@FreeBSD.org>
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
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include "namespace.h"
#include <pthread.h>
#include "un-namespace.h"
#include "libc_private.h"

/* This implements pthread_once() for the single-threaded case. */
static int
_libc_once(pthread_once_t *once_control, void (*init_routine)(void))
{

	if (once_control->state == PTHREAD_DONE_INIT)
		return (0);
	init_routine();
	once_control->state = PTHREAD_DONE_INIT;
	return (0);
}

/*
 * This is the internal interface provided to libc.  It will use
 * pthread_once() from the threading library in a multi-threaded
 * process and _libc_once() for a single-threaded library.  Because
 * _libc_once() uses the same ABI for the values in the pthread_once_t
 * structure as the threading library, it is safe for a process to
 * switch from _libc_once() to pthread_once() when threading is
 * enabled.
 */
int
_once(pthread_once_t *once_control, void (*init_routine)(void))
{

	if (__isthreaded)
		return (_pthread_once(once_control, init_routine));
	return (_libc_once(once_control, init_routine));
}
