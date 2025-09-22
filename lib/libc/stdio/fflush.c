/*	$OpenBSD: fflush.c,v 1.13 2025/08/08 15:58:53 yasuoka Exp $ */
/*-
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

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include "local.h"

/* Flush a single file, or (if fp is NULL) all files.  */
int
fflush(FILE *fp)
{
	int	r;

	if (fp == NULL)
		return (_fwalk(__sflush_locked));
	FLOCKFILE(fp);
	r = __sflush(fp);
	FUNLOCKFILE(fp);
	return (r);
}
DEF_STRONG(fflush);

int
__sflush(FILE *fp)
{
	unsigned char *p;
	fpos_t off;
	int n, t;

	t = fp->_flags;
	if (t & __SWR) {
		if ((p = fp->_bf._base) == NULL)
			return (0);

		n = fp->_p - p;		/* write this much */

		/*
		 * Set these immediately to avoid problems with longjmp and to
		 * allow exchange buffering (via setvbuf) in user write
		 * function.
		 */
		fp->_p = p;
		fp->_w = t & (__SLBF|__SNBF) ? 0 : fp->_bf._size;

		for (; n > 0; n -= t, p += t) {
			t = (*fp->_write)(fp->_cookie, (char *)p, n);
			if (t <= 0) {
				fp->_flags |= __SERR;
				return (EOF);
			}
		}
	} else if ((t & __SRD) && !(t & __SEOF)) {
		if (fp->_seek != __sseek || fp->_file < 0) {
			errno = EBADF;
			return EOF;
		}

		off = fp->_r;
		if (HASUB(fp)) {
			off = fp->_ur;
			FREEUB(fp);
		}
		fp->_ungetwc_inbuf = 0;

		if (t & __SOFF) {
			off = fp->_offset - off;
			__sseek(fp->_cookie, off, SEEK_SET);
		} else if (off != 0)
			__sseek(fp->_cookie, -off, SEEK_CUR);

		if ((fp->_flags & __SOFF)) {
			fp->_p = fp->_bf._base;
			fp->_r = 0;
		}
	}
	return (0);
}

int
__sflush_locked(FILE *fp)
{
	int	r;

	FLOCKFILE(fp);
	r = __sflush(fp);
	FUNLOCKFILE(fp);
	return (r);
}
