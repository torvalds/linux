/*	$OpenBSD: nl_langinfo_l.c,v 1.1 2017/09/05 03:16:13 schwarze Exp $ */
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

#include <langinfo.h>
#include <locale.h>

#include "rune.h"

char *
nl_langinfo_l(nl_item item, locale_t locale)
{
	_RuneLocale	*rl;
	const char	*s;

	if (item != CODESET)
		return nl_langinfo(item);

	rl = NULL;
	if (locale == _LOCALE_UTF8)
		rl = _Utf8RuneLocale;
	if (rl == NULL)
		rl = &_DefaultRuneLocale;

	s = rl->rl_codeset;
	if (s == NULL)
		s = "";

	return (char *)s;
}
