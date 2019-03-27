/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2018 Mark Johnston <markj@FreeBSD.org>
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

#include <sys/types.h>
#include <sys/syscall.h>
#include <errno.h>
#include <stdint.h>
#include <stdlib.h>

void *__sys_break(char *nsize);

static uintptr_t curbrk, minbrk;
static int curbrk_initted;

static int
initbrk(void)
{
	void *newbrk;

	if (!curbrk_initted) {
		newbrk = __sys_break(NULL);
		if (newbrk == (void *)-1)
			return (-1);
		curbrk = minbrk = (uintptr_t)newbrk;
		curbrk_initted = 1;
	}
	return (0);
}

static void *
mvbrk(void *addr)
{
	uintptr_t oldbrk;

	if ((uintptr_t)addr < minbrk) {
		/* Emulate legacy error handling in the syscall. */
		errno = EINVAL;
		return ((void *)-1);
	}
	if (__sys_break(addr) == (void *)-1)
		return ((void *)-1);
	oldbrk = curbrk;
	curbrk = (uintptr_t)addr;
	return ((void *)oldbrk);
}

int
brk(const void *addr)
{

	if (initbrk() == -1)
		return (-1);
	if ((uintptr_t)addr < minbrk)
		addr = (void *)minbrk;
	return (mvbrk(__DECONST(void *, addr)) == (void *)-1 ? -1 : 0);
}

int
_brk(const void *addr)
{

	if (initbrk() == -1)
		return (-1);
	return (mvbrk(__DECONST(void *, addr)) == (void *)-1 ? -1 : 0);
}

void *
sbrk(intptr_t incr)
{

	if (initbrk() == -1)
		return ((void *)-1);
	if ((incr > 0 && curbrk + incr < curbrk) ||
	    (incr < 0 && curbrk + incr > curbrk)) {
		/* Emulate legacy error handling in the syscall. */
		errno = EINVAL;
		return ((void *)-1);
	}
	return (mvbrk((void *)(curbrk + incr)));
}
