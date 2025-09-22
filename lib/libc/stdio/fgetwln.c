/*	$OpenBSD: fgetwln.c,v 1.4 2016/08/27 12:08:38 schwarze Exp $	*/

/*-
 * Copyright (c) 2002-2004 Tim J. Robbins.
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

#include <stdio.h>
#include <stdlib.h>
#include <wchar.h>
#include "local.h"

/*
 * Expand the line buffer.  Return -1 on error.
 */
static int
__slbexpand(FILE *fp, size_t newsize)
{
	void *p;

	if (fp->_lb._size / sizeof(wchar_t) >= newsize)
		return 0;
	if ((p = reallocarray(fp->_lb._base, newsize, sizeof(wchar_t))) == NULL)
		return -1;
	fp->_lb._base = p;
	fp->_lb._size = newsize * sizeof(wchar_t);
	return 0;
}

wchar_t *
fgetwln(FILE * __restrict fp, size_t *lenp)
{
	wint_t wc;
	size_t len;

	FLOCKFILE(fp);
	_SET_ORIENTATION(fp, 1);

	len = 0;
	while ((wc = __fgetwc_unlock(fp)) != WEOF) {
#define	GROW	512
		if (len >= fp->_lb._size / sizeof(wchar_t) &&
		    __slbexpand(fp, len + GROW)) {
			fp->_flags |= __SERR;
			goto error;
		}
		*((wchar_t *)fp->_lb._base + len++) = wc;
		if (wc == L'\n')
			break;
	}

	/*
	 * The following test assumes that fgetwc() fails when
	 * feof() is already set, and that fgetwc() will never
	 * set feof() in the same call where it also sets ferror()
	 * or returns non-WEOF.
	 * Testing ferror() would not be better because fgetwc()
	 * may succeed even when ferror() is already set.
	 */

	if (len == 0 || (wc == WEOF && !__sfeof(fp)))
		goto error;

	FUNLOCKFILE(fp);
	*lenp = len;
	return (wchar_t *)fp->_lb._base;

error:
	FUNLOCKFILE(fp);
	*lenp = 0;
	return NULL;
}
