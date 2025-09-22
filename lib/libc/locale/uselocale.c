/*	$OpenBSD: uselocale.c,v 1.1 2017/09/05 03:16:13 schwarze Exp $ */
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
#include <tib.h>

#include "rune.h"

locale_t
uselocale(locale_t newloc)
{
	struct tib		*tib;
	_RuneLocale		*oldrunes;

	tib = TIB_GET();
	oldrunes = tib->tib_locale;

	if (newloc == _LOCALE_UTF8 && _Utf8RuneLocale != NULL)
		tib->tib_locale = _Utf8RuneLocale;
	else if (newloc == _LOCALE_C)
		tib->tib_locale = &_DefaultRuneLocale;
	else if (newloc == LC_GLOBAL_LOCALE)
		tib->tib_locale = NULL;
	else if (newloc != _LOCALE_NONE) {
		errno = EINVAL;
		return _LOCALE_NONE;
	}
	return oldrunes == NULL ? LC_GLOBAL_LOCALE :
	    oldrunes == _Utf8RuneLocale ? _LOCALE_UTF8 : _LOCALE_C;
}
