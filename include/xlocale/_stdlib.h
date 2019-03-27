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

/*
 * Extended locale versions of the locale-aware functions from stdlib.h.
 *
 * Include <stdlib.h> before <xlocale.h> to expose these.
 */
double			 atof_l(const char *, locale_t);
int			 atoi_l(const char *, locale_t);
long			 atol_l(const char *, locale_t);
long long		 atoll_l(const char *, locale_t);
int			 mblen_l(const char *, size_t, locale_t);
size_t			 mbstowcs_l(wchar_t * __restrict,
			    const char * __restrict, size_t, locale_t);
int			 mbtowc_l(wchar_t * __restrict,
			    const char * __restrict, size_t, locale_t);
double			 strtod_l(const char *, char **, locale_t);
float			 strtof_l(const char *, char **, locale_t);
long			 strtol_l(const char *, char **, int, locale_t);
long double		 strtold_l(const char *, char **, locale_t);
long long		 strtoll_l(const char *, char **, int, locale_t);
unsigned long		 strtoul_l(const char *, char **, int, locale_t);
unsigned long long	 strtoull_l(const char *, char **, int, locale_t);
size_t			 wcstombs_l(char * __restrict,
			    const wchar_t * __restrict, size_t, locale_t);
int			 wctomb_l(char *, wchar_t, locale_t);

int			 ___mb_cur_max_l(locale_t);
#define MB_CUR_MAX_L(x) ((size_t)___mb_cur_max_l(x))

