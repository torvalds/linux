/*	$OpenBSD: citrus_none.c,v 1.11 2021/05/08 09:38:29 martijn Exp $ */
/*	$NetBSD: citrus_none.c,v 1.18 2008/06/14 16:01:07 tnozaki Exp $	*/

/*-
 * Copyright (c)2002 Citrus Project,
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

#include <errno.h>
#include <string.h>
#include <wchar.h>

#include "citrus_ctype.h"

static char	 wrapv(unsigned char);


/*
 * Convert an unsigned char value into a char value without relying on
 * signed overflow behavior.
 */
static char
wrapv(unsigned char ch)
{
	if (ch >= 0x80)
		return (int)ch - 0x100;
	else
		return ch;
}

size_t
_citrus_none_ctype_mbrtowc(wchar_t * __restrict pwc,
    const char * __restrict s, size_t n)
{
	if (s == NULL)
		return 0;
	if (n == 0)
		return -2;
	if (pwc != NULL)
		*pwc = (wchar_t)(unsigned char)*s;
	return *s != '\0';
}

size_t
_citrus_none_ctype_mbsnrtowcs(wchar_t * __restrict dst,
    const char ** __restrict src, size_t nmc, size_t len)
{
	size_t i;

	if (dst == NULL)
		return strnlen(*src, nmc);

	for (i = 0; i < nmc && i < len; i++)
		if ((dst[i] = (wchar_t)(unsigned char)(*src)[i]) == L'\0') {
			*src = NULL;
			return i;
		}

	*src += i;
	return i;
}

size_t
_citrus_none_ctype_wcrtomb(char * __restrict s, wchar_t wc)
{
	if (s == NULL)
		return 1;

	if (wc < 0 || wc > 0xff) {
		errno = EILSEQ;
		return -1;
	}

	*s = wrapv(wc);
	return 1;
}

size_t
_citrus_none_ctype_wcsnrtombs(char * __restrict dst,
    const wchar_t ** __restrict src, size_t nwc, size_t len)
{
	size_t i;

	if (dst == NULL) {
		for (i = 0; i < nwc; i++) {
			wchar_t wc = (*src)[i];
			if (wc < 0 || wc > 0xff) {
				errno = EILSEQ;
				return -1;
			}
			if (wc == L'\0')
				return i;
		}
		return i;
	}

	for (i = 0; i < nwc && i < len; i++) {
		wchar_t wc = (*src)[i];
		if (wc < 0 || wc > 0xff) {
			*src += i;
			errno = EILSEQ;
			return -1;
		}
		dst[i] = wrapv(wc);
		if (wc == L'\0') {
			*src = NULL;
			return i;
		}
	}
	*src += i;
	return i;
}
