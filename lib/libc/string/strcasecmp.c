/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 1987, 1993
 *	The Regents of the University of California.  All rights reserved.
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
 * 3. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#if defined(LIBC_SCCS) && !defined(lint)
static char sccsid[] = "@(#)strcasecmp.c	8.1 (Berkeley) 6/4/93";
#endif /* LIBC_SCCS and not lint */
#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <strings.h>
#include <ctype.h>
#include "xlocale_private.h"

int
strcasecmp_l(const char *s1, const char *s2, locale_t locale)
{
	const u_char
			*us1 = (const u_char *)s1,
			*us2 = (const u_char *)s2;
	FIX_LOCALE(locale);

	while (tolower_l(*us1, locale) == tolower_l(*us2++, locale))
		if (*us1++ == '\0')
			return (0);
	return (tolower_l(*us1, locale) - tolower_l(*--us2, locale));
}
int
strcasecmp(const char *s1, const char *s2)
{
	return strcasecmp_l(s1, s2, __get_locale());
}

int
strncasecmp_l(const char *s1, const char *s2, size_t n, locale_t locale)
{
	FIX_LOCALE(locale);
	if (n != 0) {
		const u_char
				*us1 = (const u_char *)s1,
				*us2 = (const u_char *)s2;

		do {
			if (tolower_l(*us1, locale) != tolower_l(*us2++, locale))
				return (tolower_l(*us1, locale) - tolower_l(*--us2, locale));
			if (*us1++ == '\0')
				break;
		} while (--n != 0);
	}
	return (0);
}

int
strncasecmp(const char *s1, const char *s2, size_t n)
{
	return strncasecmp_l(s1, s2, n, __get_locale());
}
