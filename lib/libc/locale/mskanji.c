/*-
 * SPDX-License-Identifier: BSD-4-Clause
 *
 * Copyright 2013 Garrett D'Amore <garrett@damore.org>
 * Copyright 2010 Nexenta Systems, Inc.  All rights reserved.
 * Copyright (c) 2002-2004 Tim J. Robbins. All rights reserved.
 *
 *    ja_JP.SJIS locale table for BSD4.4/rune
 *    version 1.0
 *    (C) Sin'ichiro MIYATANI / Phase One, Inc
 *    May 12, 1995
 *
 * Copyright (c) 2011 The FreeBSD Foundation
 * All rights reserved.
 * Portions of this software were developed by David Chisnall
 * under sponsorship from the FreeBSD Foundation.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *      This product includes software developed by Phase One, Inc.
 * 4. The name of Phase One, Inc. may be used to endorse or promote products
 *    derived from this software without specific prior written permission.
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
static char sccsid[] = "@(#)mskanji.c	1.0 (Phase One) 5/5/95";
#endif /* LIBC_SCCS and not lint */
#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/types.h>
#include <errno.h>
#include <runetype.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#include "mblocal.h"

extern int __mb_sb_limit;

static size_t	_MSKanji_mbrtowc(wchar_t * __restrict, const char * __restrict,
		    size_t, mbstate_t * __restrict);
static int	_MSKanji_mbsinit(const mbstate_t *);
static size_t	_MSKanji_wcrtomb(char * __restrict, wchar_t,
		    mbstate_t * __restrict);
static size_t	_MSKanji_mbsnrtowcs(wchar_t * __restrict,
		    const char ** __restrict, size_t, size_t,
		    mbstate_t * __restrict);
static size_t	_MSKanji_wcsnrtombs(char * __restrict,
		    const wchar_t ** __restrict, size_t, size_t,
		    mbstate_t * __restrict);

typedef struct {
	wchar_t	ch;
} _MSKanjiState;

int
_MSKanji_init(struct xlocale_ctype *l, _RuneLocale *rl)
{

	l->__mbrtowc = _MSKanji_mbrtowc;
	l->__wcrtomb = _MSKanji_wcrtomb;
	l->__mbsnrtowcs = _MSKanji_mbsnrtowcs;
	l->__wcsnrtombs = _MSKanji_wcsnrtombs;
	l->__mbsinit = _MSKanji_mbsinit;
	l->runes = rl;
	l->__mb_cur_max = 2;
	l->__mb_sb_limit = 224;
	return (0);
}

static int
_MSKanji_mbsinit(const mbstate_t *ps)
{

	return (ps == NULL || ((const _MSKanjiState *)ps)->ch == 0);
}

static size_t
_MSKanji_mbrtowc(wchar_t * __restrict pwc, const char * __restrict s, size_t n,
    mbstate_t * __restrict ps)
{
	_MSKanjiState *ms;
	wchar_t wc;

	ms = (_MSKanjiState *)ps;

	if ((ms->ch & ~0xFF) != 0) {
		/* Bad conversion state. */
		errno = EINVAL;
		return ((size_t)-1);
	}

	if (s == NULL) {
		s = "";
		n = 1;
		pwc = NULL;
	}

	if (n == 0)
		/* Incomplete multibyte sequence */
		return ((size_t)-2);

	if (ms->ch != 0) {
		if (*s == '\0') {
			errno = EILSEQ;
			return ((size_t)-1);
		}
		wc = (ms->ch << 8) | (*s & 0xFF);
		if (pwc != NULL)
			*pwc = wc;
		ms->ch = 0;
		return (1);
	}
	wc = *s++ & 0xff;
	if ((wc > 0x80 && wc < 0xa0) || (wc >= 0xe0 && wc < 0xfd)) {
		if (n < 2) {
			/* Incomplete multibyte sequence */
			ms->ch = wc;
			return ((size_t)-2);
		}
		if (*s == '\0') {
			errno = EILSEQ;
			return ((size_t)-1);
		}
		wc = (wc << 8) | (*s++ & 0xff);
		if (pwc != NULL)
			*pwc = wc;
		return (2);
	} else {
		if (pwc != NULL)
			*pwc = wc;
		return (wc == L'\0' ? 0 : 1);
	}
}

static size_t
_MSKanji_wcrtomb(char * __restrict s, wchar_t wc, mbstate_t * __restrict ps)
{
	_MSKanjiState *ms;
	int len, i;

	ms = (_MSKanjiState *)ps;

	if (ms->ch != 0) {
		errno = EINVAL;
		return ((size_t)-1);
	}

	if (s == NULL)
		/* Reset to initial shift state (no-op) */
		return (1);
	len = (wc > 0x100) ? 2 : 1;
	for (i = len; i-- > 0; )
		*s++ = wc >> (i << 3);
	return (len);
}

static size_t
_MSKanji_mbsnrtowcs(wchar_t * __restrict dst,
    const char ** __restrict src, size_t nms,
    size_t len, mbstate_t * __restrict ps)
{
	return (__mbsnrtowcs_std(dst, src, nms, len, ps, _MSKanji_mbrtowc));
}

static size_t
_MSKanji_wcsnrtombs(char * __restrict dst,
    const wchar_t ** __restrict src, size_t nwc,
    size_t len, mbstate_t * __restrict ps)
{
	return (__wcsnrtombs_std(dst, src, nwc, len, ps, _MSKanji_wcrtomb));
}
