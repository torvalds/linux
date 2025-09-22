/*	$OpenBSD: ungetwc.c,v 1.7 2025/08/08 15:58:53 yasuoka Exp $	*/
/* $NetBSD: ungetwc.c,v 1.2 2003/01/18 11:29:59 thorpej Exp $ */

/*-
 * Copyright (c)2001 Citrus Project,
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
 *
 * $Citrus$
 */

#include <errno.h>
#include <stdio.h>
#include <wchar.h>
#include "local.h"

wint_t
__ungetwc(wint_t wc, FILE *fp)
{
	if (wc == WEOF)
		return WEOF;

	_SET_ORIENTATION(fp, 1);
	/*
	 * XXX since we have no way to transform a wchar string to
	 * a char string in reverse order, we can't use ungetc.
	 */
	if (fp->_ungetwc_inbuf >= FILE_UNGETWC_BUFSIZE) {
		return WEOF;
	}

	fp->_ungetwc_buf[fp->_ungetwc_inbuf++] = wc;
	__sclearerr(fp);

	return wc;
}

wint_t
ungetwc(wint_t wc, FILE *fp)
{
	wint_t r;

	FLOCKFILE(fp);
	r = __ungetwc(wc, fp);
	FUNLOCKFILE(fp);
	return (r);
}
DEF_STRONG(ungetwc);
