/*      $OpenBSD: citrus_ctype.h,v 1.5 2016/09/05 09:47:02 schwarze Exp $ */

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
 *
 */

#ifndef _CITRUS_CTYPE_H_
#define _CITRUS_CTYPE_H_

#define _CITRUS_UTF8_MB_CUR_MAX 4

__BEGIN_HIDDEN_DECLS
size_t	_citrus_none_ctype_mbrtowc(wchar_t * __restrict,
		const char * __restrict, size_t);
size_t  _citrus_none_ctype_mbsnrtowcs(wchar_t * __restrict,
		const char ** __restrict, size_t, size_t);
size_t  _citrus_none_ctype_wcrtomb(char * __restrict, wchar_t);
size_t  _citrus_none_ctype_wcsnrtombs(char * __restrict,
		const wchar_t ** __restrict, size_t, size_t);

size_t	_citrus_utf8_ctype_mbrtowc(wchar_t * __restrict,
		const char * __restrict, size_t, mbstate_t * __restrict);
int     _citrus_utf8_ctype_mbsinit(const mbstate_t * __restrict);
size_t  _citrus_utf8_ctype_mbsnrtowcs(wchar_t * __restrict,
		const char ** __restrict, size_t, size_t,
		mbstate_t * __restrict);
size_t  _citrus_utf8_ctype_wcrtomb(char * __restrict, wchar_t,
		mbstate_t * __restrict);
size_t  _citrus_utf8_ctype_wcsnrtombs(char * __restrict,
		const wchar_t ** __restrict, size_t, size_t,
		mbstate_t * __restrict);
__END_HIDDEN_DECLS

#endif
