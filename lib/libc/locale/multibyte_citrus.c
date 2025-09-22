/*	$OpenBSD: multibyte_citrus.c,v 1.8 2017/09/05 03:16:13 schwarze Exp $ */
/*	$NetBSD: multibyte_amd1.c,v 1.7 2009/01/11 02:46:28 christos Exp $ */

/*-
 * Copyright (c)2002, 2008 Citrus Project,
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

#include <sys/types.h>
#include <stdint.h>
#include <stdlib.h>
#include <wchar.h>

#include "citrus_ctype.h"

int
mbsinit(const mbstate_t *ps)
{
	if (ps == NULL || __mb_cur_max() == 1)
		return 1;
	return _citrus_utf8_ctype_mbsinit(ps);
}
DEF_STRONG(mbsinit);

size_t
mbrtowc(wchar_t *pwc, const char *s, size_t n, mbstate_t *ps)
{
	static mbstate_t mbs;

	if (ps == NULL)
		ps = &mbs;
	if (__mb_cur_max() == 1)
		return _citrus_none_ctype_mbrtowc(pwc, s, n);
	return _citrus_utf8_ctype_mbrtowc(pwc, s, n, ps);
}
DEF_STRONG(mbrtowc);

size_t
mbsrtowcs(wchar_t *dst, const char **src, size_t len, mbstate_t *ps)
{
	static mbstate_t mbs;

	if (ps == NULL)
		ps = &mbs;
	return (mbsnrtowcs(dst, src, SIZE_MAX, len, ps));
}
DEF_STRONG(mbsrtowcs);

size_t
mbsnrtowcs(wchar_t *dst, const char **src, size_t nmc, size_t len,
    mbstate_t *ps)
{
	static mbstate_t mbs;

	if (ps == NULL)
		ps = &mbs;
	if (__mb_cur_max() == 1)
		return _citrus_none_ctype_mbsnrtowcs(dst, src, nmc, len);
	return _citrus_utf8_ctype_mbsnrtowcs(dst, src, nmc, len, ps);
}
DEF_WEAK(mbsnrtowcs);

size_t
wcrtomb(char *s, wchar_t wc, mbstate_t *ps)
{
	static mbstate_t mbs;

	if (ps == NULL)
		ps = &mbs;
	if (__mb_cur_max() == 1)
		return _citrus_none_ctype_wcrtomb(s, wc);
	return _citrus_utf8_ctype_wcrtomb(s, wc, ps);
}
DEF_STRONG(wcrtomb);

size_t
wcsrtombs(char *dst, const wchar_t **src, size_t len, mbstate_t *ps)
{
	static mbstate_t mbs;

	if (ps == NULL)
		ps = &mbs;
	return (wcsnrtombs(dst, src, SIZE_MAX, len, ps));
}
DEF_STRONG(wcsrtombs);

size_t
wcsnrtombs(char *dst, const wchar_t **src, size_t nwc, size_t len,
    mbstate_t *ps)
{
	static mbstate_t mbs;

	if (ps == NULL)
		ps = &mbs;
	if (__mb_cur_max() == 1)
		return _citrus_none_ctype_wcsnrtombs(dst, src, nwc, len);
	return _citrus_utf8_ctype_wcsnrtombs(dst, src, nwc, len, ps);
}
DEF_WEAK(wcsnrtombs);
