/*-
 * Copyright (c) 2017 Juniper Networks.  All rights reserved.
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
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
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
#include <sys/types.h>
#include <machine/atomic.h>
#include <errno.h>
#include <pthread.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "un-namespace.h"
#include "libc_private.h"

/*
 * Rationale recommends allocating new memory each time.
 */
static constraint_handler_t *_ch = NULL;
static pthread_mutex_t ch_lock = PTHREAD_MUTEX_INITIALIZER;

constraint_handler_t
set_constraint_handler_s(constraint_handler_t handler)
{
	constraint_handler_t *new, *old, ret;

	new = malloc(sizeof(constraint_handler_t));
	if (new == NULL)
		return (NULL);
	*new = handler;
	if (__isthreaded)
		_pthread_mutex_lock(&ch_lock);
	old = _ch;
	_ch = new;
	if (__isthreaded)
		_pthread_mutex_unlock(&ch_lock);
	if (old == NULL) {
		ret = NULL;
	} else {
		ret = *old;
		free(old);
	}
	return (ret);
}

void
__throw_constraint_handler_s(const char * restrict msg, errno_t error)
{
	constraint_handler_t ch;

	if (__isthreaded)
		_pthread_mutex_lock(&ch_lock);
	ch = _ch != NULL ? *_ch : NULL;
	if (__isthreaded)
		_pthread_mutex_unlock(&ch_lock);
	if (ch != NULL)
		ch(msg, NULL, error);
}

void
abort_handler_s(const char * restrict msg, void * restrict ptr __unused,
    errno_t error __unused)
{
	static const char ahs[] = "abort_handler_s : ";

	(void) _write(STDERR_FILENO, ahs, sizeof(ahs) - 1);
	(void) _write(STDERR_FILENO, msg, strlen(msg));
	(void) _write(STDERR_FILENO, "\n", 1);
	abort();
}

void
ignore_handler_s(const char * restrict msg __unused,
    void * restrict ptr __unused, errno_t error __unused)
{
}
