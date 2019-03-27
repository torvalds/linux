/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2009 David Schultz <das@FreeBSD.org>
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
#include <sys/param.h>
#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "un-namespace.h"

#include "libc_private.h"
#include "local.h"

static inline size_t
p2roundup(size_t n)
{

	if (!powerof2(n)) {
		n--;
		n |= n >> 1;
		n |= n >> 2;
		n |= n >> 4;
		n |= n >> 8;
		n |= n >> 16;
#if SIZE_T_MAX > 0xffffffffU
		n |= n >> 32;
#endif
		n++;
	}
	return (n);
}

/*
 * Expand *linep to hold len bytes (up to SSIZE_MAX + 1).
 */
static inline int
expandtofit(char ** __restrict linep, size_t len, size_t * __restrict capp)
{
	char *newline;
	size_t newcap;

	if (len > (size_t)SSIZE_MAX + 1) {
		errno = EOVERFLOW;
		return (-1);
	}
	if (len > *capp) {
		if (len == (size_t)SSIZE_MAX + 1)	/* avoid overflow */
			newcap = (size_t)SSIZE_MAX + 1;
		else
			newcap = p2roundup(len);
		newline = realloc(*linep, newcap);
		if (newline == NULL)
			return (-1);
		*capp = newcap;
		*linep = newline;
	}
	return (0);
}

/*
 * Append the src buffer to the *dstp buffer. The buffers are of
 * length srclen and *dstlenp, respectively, and dst has space for
 * *dstlenp bytes. After the call, *dstlenp and *dstcapp are updated
 * appropriately, and *dstp is reallocated if needed. Returns 0 on
 * success, -1 on allocation failure.
 */
static int
sappend(char ** __restrict dstp, size_t * __restrict dstlenp,
	size_t * __restrict dstcapp, char * __restrict src, size_t srclen)
{

	/* ensure room for srclen + dstlen + terminating NUL */
	if (expandtofit(dstp, srclen + *dstlenp + 1, dstcapp))
		return (-1);
	memcpy(*dstp + *dstlenp, src, srclen);
	*dstlenp += srclen;
	return (0);
}

ssize_t
getdelim(char ** __restrict linep, size_t * __restrict linecapp, int delim,
	 FILE * __restrict fp)
{
	u_char *endp;
	size_t linelen;

	FLOCKFILE_CANCELSAFE(fp);
	ORIENT(fp, -1);

	if (linep == NULL || linecapp == NULL) {
		errno = EINVAL;
		goto error;
	}

	if (*linep == NULL)
		*linecapp = 0;

	if (fp->_r <= 0 && __srefill(fp)) {
		/* If fp is at EOF already, we just need space for the NUL. */
		if (!__sfeof(fp) || expandtofit(linep, 1, linecapp))
			goto error;
		(*linep)[0] = '\0';
		linelen = -1;
		goto end;
	}

	linelen = 0;
	while ((endp = memchr(fp->_p, delim, fp->_r)) == NULL) {
		if (sappend(linep, &linelen, linecapp, fp->_p, fp->_r))
			goto error;
		if (__srefill(fp)) {
			if (!__sfeof(fp))
				goto error;
			goto done;	/* hit EOF */
		}
	}
	endp++;	/* snarf the delimiter, too */
	if (sappend(linep, &linelen, linecapp, fp->_p, endp - fp->_p))
		goto error;
	fp->_r -= endp - fp->_p;
	fp->_p = endp;
done:
	/* Invariant: *linep has space for at least linelen+1 bytes. */
	(*linep)[linelen] = '\0';
end:
	FUNLOCKFILE_CANCELSAFE();
	return (linelen);

error:
	fp->_flags |= __SERR;
	linelen = -1;
	goto end;
}
