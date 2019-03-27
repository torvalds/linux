/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2011, 2012 The FreeBSD Foundation
 * All rights reserved.
 *
 * This software was developed by David Chisnall under sponsorship from
 * the FreeBSD Foundation.
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
 * $FreeBSD$
 */

#ifndef _LOCALE_T_DEFINED
#define _LOCALE_T_DEFINED
typedef struct	_xlocale *locale_t;
#endif

#ifndef _XLOCALE_WCHAR1_H
#define _XLOCALE_WCHAR1_H
int			 wcscasecmp_l(const wchar_t *, const wchar_t *,
			   locale_t);
int			 wcsncasecmp_l(const wchar_t *, const wchar_t *, size_t,
			   locale_t);
int			 wcscoll_l(const wchar_t *, const wchar_t *, locale_t);
size_t			 wcsxfrm_l(wchar_t * __restrict,
			   const wchar_t * __restrict, size_t, locale_t);

#endif /* _XLOCALE_WCHAR1_H */

/*
 * Only declare the non-POSIX functions if we're included from xlocale.h.
 */

#ifdef _XLOCALE_H_
#ifndef _XLOCALE_WCHAR2_H
#define _XLOCALE_WCHAR2_H

wint_t			 btowc_l(int, locale_t);
wint_t			 fgetwc_l(FILE *, locale_t);
wchar_t			*fgetws_l(wchar_t * __restrict, int, FILE * __restrict,
			    locale_t);
wint_t			 fputwc_l(wchar_t, FILE *, locale_t);
int			 fputws_l(const wchar_t * __restrict, FILE * __restrict,
			   locale_t);
int			 fwprintf_l(FILE * __restrict, locale_t,
			    const wchar_t * __restrict, ...);
int			 fwscanf_l(FILE * __restrict, locale_t,
			    const wchar_t * __restrict, ...);
wint_t			 getwc_l(FILE *, locale_t);
wint_t			 getwchar_l(locale_t);
size_t			 mbrlen_l(const char * __restrict, size_t,
			   mbstate_t * __restrict, locale_t);
size_t			 mbrtowc_l(wchar_t * __restrict,
			    const char * __restrict, size_t,
			    mbstate_t * __restrict, locale_t);
int			 mbsinit_l(const mbstate_t *, locale_t);
size_t			 mbsrtowcs_l(wchar_t * __restrict,
			    const char ** __restrict, size_t,
			    mbstate_t * __restrict, locale_t);
wint_t			 putwc_l(wchar_t, FILE *, locale_t);
wint_t			 putwchar_l(wchar_t, locale_t);
int			 swprintf_l(wchar_t * __restrict, size_t n, locale_t,
			    const wchar_t * __restrict, ...);
int			 swscanf_l(const wchar_t * __restrict, locale_t,
			   const wchar_t * __restrict, ...);
wint_t			 ungetwc_l(wint_t, FILE *, locale_t);
int			 vfwprintf_l(FILE * __restrict, locale_t,
			    const wchar_t * __restrict, __va_list);
int			 vswprintf_l(wchar_t * __restrict, size_t n, locale_t,
			    const wchar_t * __restrict, __va_list);
int			 vwprintf_l(locale_t, const wchar_t * __restrict,
			    __va_list);
size_t			 wcrtomb_l(char * __restrict, wchar_t,
			    mbstate_t * __restrict, locale_t);
size_t			 wcsftime_l(wchar_t * __restrict, size_t,
			    const wchar_t * __restrict,
			    const struct tm * __restrict, locale_t);
size_t			 wcsrtombs_l(char * __restrict,
			    const wchar_t ** __restrict, size_t,
			    mbstate_t * __restrict, locale_t);
double			 wcstod_l(const wchar_t * __restrict,
			    wchar_t ** __restrict, locale_t);
long			 wcstol_l(const wchar_t * __restrict,
			    wchar_t ** __restrict, int, locale_t);
unsigned long		 wcstoul_l(const wchar_t * __restrict,
			    wchar_t ** __restrict, int, locale_t);
int			 wcswidth_l(const wchar_t *, size_t, locale_t);
int			 wctob_l(wint_t, locale_t);
int			 wcwidth_l(wchar_t, locale_t);
int			 wprintf_l(locale_t, const wchar_t * __restrict, ...);
int			 wscanf_l(locale_t, const wchar_t * __restrict, ...);
int			 vfwscanf_l(FILE * __restrict, locale_t,
			    const wchar_t * __restrict, __va_list);
int			 vswscanf_l(const wchar_t * __restrict, locale_t,
			    const wchar_t *__restrict, __va_list);
int			 vwscanf_l(locale_t, const wchar_t * __restrict,
			    __va_list);
float			 wcstof_l(const wchar_t * __restrict,
			    wchar_t ** __restrict, locale_t);
long double		 wcstold_l(const wchar_t * __restrict,
			    wchar_t ** __restrict, locale_t);
long long		 wcstoll_l(const wchar_t * __restrict,
			    wchar_t ** __restrict, int, locale_t);
unsigned long long	 wcstoull_l(const wchar_t * __restrict,
			    wchar_t ** __restrict, int, locale_t);
size_t			 mbsnrtowcs_l(wchar_t * __restrict,
			    const char ** __restrict, size_t, size_t,
			    mbstate_t * __restrict, locale_t);
size_t			 wcsnrtombs_l(char * __restrict,
			    const wchar_t ** __restrict, size_t, size_t,
			    mbstate_t * __restrict, locale_t);

#endif /* _XLOCALE_WCHAR_H */
#endif /* _XLOCALE_H_ */
