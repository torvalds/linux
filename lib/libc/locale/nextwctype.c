/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2004 Tim J. Robbins.
 * All rights reserved.
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

#include <runetype.h>
#include <wchar.h>
#include <wctype.h>
#include "mblocal.h"

wint_t
nextwctype_l(wint_t wc, wctype_t wct, locale_t locale)
{
	size_t lim;
	FIX_LOCALE(locale);
	_RuneLocale *runes = XLOCALE_CTYPE(locale)->runes;
	_RuneRange *rr = &runes->__runetype_ext;
	_RuneEntry *base, *re;
	int noinc;

	noinc = 0;
	if (wc < _CACHED_RUNES) {
		wc++;
		while (wc < _CACHED_RUNES) {
			if (runes->__runetype[wc] & wct)
				return (wc);
			wc++;
		}
		wc--;
	}
	if (rr->__ranges != NULL && wc < rr->__ranges[0].__min) {
		wc = rr->__ranges[0].__min;
		noinc = 1;
	}

	/* Binary search -- see bsearch.c for explanation. */
	base = rr->__ranges;
	for (lim = rr->__nranges; lim != 0; lim >>= 1) {
		re = base + (lim >> 1);
		if (re->__min <= wc && wc <= re->__max)
			goto found;
		else if (wc > re->__max) {
			base = re + 1;
			lim--;
		}
	}
	return (-1);
found:
	if (!noinc)
		wc++;
	if (re->__min <= wc && wc <= re->__max) {
		if (re->__types != NULL) {
			for (; wc <= re->__max; wc++)
				if (re->__types[wc - re->__min] & wct)
					return (wc);
		} else if (re->__map & wct)
			return (wc);
	}
	while (++re < rr->__ranges + rr->__nranges) {
		wc = re->__min;
		if (re->__types != NULL) {
			for (; wc <= re->__max; wc++)
				if (re->__types[wc - re->__min] & wct)
					return (wc);
		} else if (re->__map & wct)
			return (wc);
	}
	return (-1);
}
wint_t
nextwctype(wint_t wc, wctype_t wct)
{
	return nextwctype_l(wc, wct, __get_locale());
}
