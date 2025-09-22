/*	$OpenBSD: _get_locname.c,v 1.1 2017/09/05 03:16:13 schwarze Exp $ */
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
#include <paths.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "rune.h"
#include "rune_local.h"

/*
 * Supplement the input with locale(1) variables from the
 * environment, validate it, and initialize UTF-8 when needed.
 * Return NULL on error, "" if unset (for LC_ALL only),
 * or the name of the locale that takes effect.
 */
const char *
_get_locname(int category, const char *locname)
{
	static const char *const catname[_LC_LAST - 1] = {
		"LC_COLLATE",	"LC_CTYPE",	"LC_MONETARY",
		"LC_NUMERIC",	"LC_TIME",	"LC_MESSAGES"
	};

	FILE		*fp;
	const char	*cp;
	int		 save_errno;

	/* For empty input, inspect the environment. */
	if (*locname == '\0')
		locname = getenv("LC_ALL");

	if (category != LC_ALL) {
		if (locname == NULL || *locname == '\0')
			locname = getenv(catname[category - 1]);
		if (locname == NULL || *locname == '\0')
			locname = getenv("LANG");
	}

	/*
	 * If still empty, treat LC_ALL as unset, but
	 * fall back to the default locale for other categories.
	 */
	if (locname == NULL || *locname == '\0')
		locname = category == LC_ALL ? "" : "C";

	/* Accept and ignore all language and territory prefixes. */
	if ((cp = strrchr(locname, '.')) == NULL)
		return locname;

	/* Reject all encodings except ASCII and UTF-8. */
	if (strcmp(cp + 1, "UTF-8") != 0)
		return NULL;

	/* Initialize the UTF-8 character encoding locale, if needed. */
	if (_Utf8RuneLocale != NULL)
		return locname;
	save_errno = errno;
	if ((fp = fopen(_PATH_LOCALE "/UTF-8/LC_CTYPE", "re")) != NULL) {
		_Utf8RuneLocale = _Read_RuneMagi(fp);
		fclose(fp);
	}
	errno = save_errno;
	return _Utf8RuneLocale == NULL ? NULL : locname;
}
