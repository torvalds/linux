/*	$OpenBSD: newlocale.c,v 1.2 2019/03/29 12:34:44 schwarze Exp $ */
/*
 * Copyright (c) 2017 Ingo Schwarze <schwarze@openbsd.org>
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include <errno.h>
#include <locale.h>
#include <string.h>

#include "rune.h"

locale_t
newlocale(int mask, const char *locname, locale_t oldloc)
{
	int	 ic, flag;

	/* Invalid input. */
	if (locname == NULL || mask & ~LC_ALL_MASK) {
		errno = EINVAL;
		return _LOCALE_NONE;
	}

	/* Check the syntax for all selected categories. */
	for (ic = flag = 1; ic < _LC_LAST; ic++) {
		flag <<= 1;
		if (ic != LC_CTYPE && mask & flag &&
		    _get_locname(ic, locname) == NULL) {
			errno = ENOENT;
			return _LOCALE_NONE;
		}
	}

	/* Only character encoding has thread-specific effects. */
	if ((mask & LC_CTYPE_MASK) == 0)
		return oldloc == _LOCALE_UTF8 ? _LOCALE_UTF8 : _LOCALE_C;

	/* The following may initialize UTF-8 for later use. */
	if ((locname = _get_locname(LC_CTYPE, locname)) == NULL) {
		errno = ENOENT;
		return _LOCALE_NONE;
	}
	return strchr(locname, '.') == NULL ? _LOCALE_C : _LOCALE_UTF8;
}
