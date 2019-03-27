/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2002 Tim J. Robbins.
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

#include <ctype.h>
#include <string.h>
#include <wctype.h>
#include <xlocale.h>

#undef iswctype
int
iswctype(wint_t wc, wctype_t charclass)
{
	return (__istype(wc, charclass));
}
int
iswctype_l(wint_t wc, wctype_t charclass, locale_t locale)
{
	return __istype_l(wc, charclass, locale);
}

/*
 * IMPORTANT: The 0 in the call to this function in wctype() must be changed to
 * __get_locale() if wctype_l() is ever modified to actually use the locale
 * parameter.
 */
wctype_t
wctype_l(const char *property, locale_t locale)
{
	const char *propnames = 
		"alnum\0"
		"alpha\0"
		"blank\0"
		"cntrl\0"
		"digit\0"
		"graph\0"
		"lower\0"
		"print\0"
		"punct\0"
		"space\0"
		"upper\0"
		"xdigit\0"
		"ideogram\0"	/* BSD extension */
		"special\0"	/* BSD extension */
		"phonogram\0"	/* BSD extension */
		"number\0"	/* BSD extension */
		"rune\0";	/* BSD extension */
	static const wctype_t propmasks[] = {
		_CTYPE_A|_CTYPE_N,
		_CTYPE_A,
		_CTYPE_B,
		_CTYPE_C,
		_CTYPE_D,
		_CTYPE_G,
		_CTYPE_L,
		_CTYPE_R,
		_CTYPE_P,
		_CTYPE_S,
		_CTYPE_U,
		_CTYPE_X,
		_CTYPE_I,
		_CTYPE_T,
		_CTYPE_Q,
		_CTYPE_N,
		0xFFFFFF00L
	};
	size_t len1, len2;
	const char *p;
	const wctype_t *q;

	len1 = strlen(property);
	q = propmasks;
	for (p = propnames; (len2 = strlen(p)) != 0; p += len2 + 1) {
		if (len1 == len2 && memcmp(property, p, len1) == 0)
			return (*q);
		q++;
	}

	return (0UL);
}

wctype_t wctype(const char *property)
{
	return wctype_l(property, 0);
}
