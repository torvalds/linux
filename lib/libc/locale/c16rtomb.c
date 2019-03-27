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

#include <errno.h>
#include <uchar.h>
#include "mblocal.h"

typedef struct {
	char16_t	lead_surrogate;
	mbstate_t	c32_mbstate;
} _Char16State;

size_t
c16rtomb_l(char * __restrict s, char16_t c16, mbstate_t * __restrict ps,
    locale_t locale)
{
	_Char16State *cs;
	char32_t c32;

	FIX_LOCALE(locale);
	if (ps == NULL)
		ps = &(XLOCALE_CTYPE(locale)->c16rtomb);
	cs = (_Char16State *)ps;

	/* If s is a null pointer, the value of parameter c16 is ignored. */
	if (s == NULL) {
		c32 = 0;
	} else if (cs->lead_surrogate >= 0xd800 &&
	    cs->lead_surrogate <= 0xdbff) {
		/* We should see a trail surrogate now. */
		if (c16 < 0xdc00 || c16 > 0xdfff) {
			errno = EILSEQ;
			return ((size_t)-1);
		}
		c32 = 0x10000 + ((cs->lead_surrogate & 0x3ff) << 10 |
		    (c16 & 0x3ff));
	} else if (c16 >= 0xd800 && c16 <= 0xdbff) {
		/* Store lead surrogate for next invocation. */
		cs->lead_surrogate = c16;
		return (0);
	} else {
		/* Regular character. */
		c32 = c16;
	}
	cs->lead_surrogate = 0;

	return (c32rtomb_l(s, c32, &cs->c32_mbstate, locale));
}

size_t
c16rtomb(char * __restrict s, char16_t c16, mbstate_t * __restrict ps)
{

	return (c16rtomb_l(s, c16, ps, __get_locale()));
}
