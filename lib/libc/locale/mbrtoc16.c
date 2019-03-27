/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2013 Ed Schouten <ed@FreeBSD.org>
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

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <uchar.h>
#include "mblocal.h"

typedef struct {
	char16_t	trail_surrogate;
	mbstate_t	c32_mbstate;
} _Char16State;

size_t
mbrtoc16_l(char16_t * __restrict pc16, const char * __restrict s, size_t n,
    mbstate_t * __restrict ps, locale_t locale)
{
	_Char16State *cs;
	char32_t c32;
	ssize_t len;

	FIX_LOCALE(locale);
	if (ps == NULL)
		ps = &(XLOCALE_CTYPE(locale)->mbrtoc16);
	cs = (_Char16State *)ps;

	/*
	 * Call straight into mbrtoc32_l() if we don't need to return a
	 * character value. According to the spec, if s is a null
	 * pointer, the value of parameter pc16 is also ignored.
	 */
	if (pc16 == NULL || s == NULL) {
		cs->trail_surrogate = 0;
		return (mbrtoc32_l(NULL, s, n, &cs->c32_mbstate, locale));
	}

	/* Return the trail surrogate from the previous invocation. */
	if (cs->trail_surrogate >= 0xdc00 && cs->trail_surrogate <= 0xdfff) {
		*pc16 = cs->trail_surrogate;
		cs->trail_surrogate = 0;
		return ((size_t)-3);
	}

	len = mbrtoc32_l(&c32, s, n, &cs->c32_mbstate, locale);
	if (len >= 0) {
		if (c32 < 0x10000) {
			/* Fits in one UTF-16 character. */
			*pc16 = c32;
		} else {
			/* Split up in a surrogate pair. */
			c32 -= 0x10000;
			*pc16 = 0xd800 | (c32 >> 10);
			cs->trail_surrogate = 0xdc00 | (c32 & 0x3ff);
		}
	}
	return (len);
}

size_t
mbrtoc16(char16_t * __restrict pc16, const char * __restrict s, size_t n,
    mbstate_t * __restrict ps)
{

	return (mbrtoc16_l(pc16, s, n, ps, __get_locale()));
}
