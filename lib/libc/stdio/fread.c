/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 1990, 1993
 *	The Regents of the University of California.  All rights reserved.
 *
 * This code is derived from software contributed to Berkeley by
 * Chris Torek.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
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

#if defined(LIBC_SCCS) && !defined(lint)
static char sccsid[] = "@(#)fread.c	8.2 (Berkeley) 12/11/93";
#endif /* LIBC_SCCS and not lint */
#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include "namespace.h"
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "un-namespace.h"
#include "local.h"
#include "libc_private.h"

/*
 * MT-safe version
 */

size_t
fread(void * __restrict buf, size_t size, size_t count, FILE * __restrict fp)
{
	size_t ret;

	FLOCKFILE_CANCELSAFE(fp);
	ret = __fread(buf, size, count, fp);
	FUNLOCKFILE_CANCELSAFE();
	return (ret);
}

size_t
__fread(void * __restrict buf, size_t size, size_t count, FILE * __restrict fp)
{
	size_t resid;
	char *p;
	int r;
	size_t total;

	/*
	 * ANSI and SUSv2 require a return value of 0 if size or count are 0.
	 */
	if ((count == 0) || (size == 0))
		return (0);

	/*
	 * Check for integer overflow.  As an optimization, first check that
	 * at least one of {count, size} is at least 2^16, since if both
	 * values are less than that, their product can't possible overflow
	 * (size_t is always at least 32 bits on FreeBSD).
	 */
	if (((count | size) > 0xFFFF) &&
	    (count > SIZE_MAX / size)) {
		errno = EINVAL;
		fp->_flags |= __SERR;
		return (0);
	}

	/*
	 * Compute the (now required to not overflow) number of bytes to
	 * read and actually do the work.
	 */
	resid = count * size;
	ORIENT(fp, -1);
	if (fp->_r < 0)
		fp->_r = 0;
	total = resid;
	p = buf;
	while (resid > (r = fp->_r)) {
		(void)memcpy((void *)p, (void *)fp->_p, (size_t)r);
		fp->_p += r;
		/* fp->_r = 0 ... done in __srefill */
		p += r;
		resid -= r;
		if (__srefill(fp)) {
			/* no more input: return partial result */
			return ((total - resid) / size);
		}
	}
	(void)memcpy((void *)p, (void *)fp->_p, resid);
	fp->_r -= resid;
	fp->_p += resid;
	return (count);
}
