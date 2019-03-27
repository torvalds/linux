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

int	 asprintf_l(char **, locale_t, const char *, ...) __printflike(3, 4);
int	 dprintf_l(int, locale_t, const char * __restrict, ...)
	    __printflike(3, 4);
int	 fprintf_l(FILE * __restrict, locale_t, const char * __restrict, ...)
	    __printflike(3, 4);
int	 fscanf_l(FILE * __restrict, locale_t, const char * __restrict, ...)
	    __scanflike(3, 4);
int	 printf_l(locale_t, const char * __restrict, ...) __printflike(2, 3);
int	 scanf_l(locale_t, const char * __restrict, ...) __scanflike(2, 3);
int	 snprintf_l(char * __restrict, size_t, locale_t,
	    const char * __restrict, ...) __printflike(4, 5);
int	 sprintf_l(char * __restrict, locale_t, const char * __restrict, ...)
	    __printflike(3, 4);
int	 sscanf_l(const char * __restrict, locale_t, const char * __restrict,
	    ...) __scanflike(3, 4);
int	 vfprintf_l(FILE * __restrict, locale_t, const char * __restrict,
	    __va_list) __printflike(3, 0);
int	 vprintf_l(locale_t, const char * __restrict, __va_list)
	    __printflike(2, 0);
int	 vsprintf_l(char * __restrict, locale_t, const char * __restrict,
	    __va_list) __printflike(3, 0);
int	 vfscanf_l(FILE * __restrict, locale_t, const char * __restrict,
	    __va_list) __scanflike(3, 0);
int	 vscanf_l(locale_t, const char * __restrict, __va_list)
	    __scanflike(2, 0);
int	 vsnprintf_l(char * __restrict, size_t, locale_t,
	    const char * __restrict, __va_list) __printflike(4, 0);
int	 vsscanf_l(const char * __restrict, locale_t, const char * __restrict,
	    __va_list) __scanflike(3, 0);
int	 vdprintf_l(int, locale_t, const char * __restrict, __va_list)
	    __printflike(3, 0);
int	 vasprintf_l(char **, locale_t, const char *, __va_list)
	    __printflike(3, 0);
