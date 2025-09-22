/* $OpenBSD: setlocale.c,v 1.4 2018/03/29 16:34:25 schwarze Exp $ */
/*
 * Copyright (c) 2015 Sebastien Marie <semarie@openbsd.org>
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

#include <err.h>
#include <locale.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <ctype.h>

/*
 * test helpers for __LINE__
 */
#define test_setlocale(_e, _c, _l) _test_setlocale(_e, _c, _l, __LINE__)
#define test_MB_CUR_MAX(_e) _test_MB_CUR_MAX(_e, __LINE__)
#define test_isalpha(_e, _c) _test_isalpha(_e, _c, __LINE__)


static void
_test_setlocale(char *expected, int category, char *locale, int line)
{
	char *result = setlocale(category, locale);

	if ((expected == NULL) || (result == NULL)) {
		if (expected == result)
			return;

		errx(1, "[%d] setlocale(%d, \"%s\")=\"%s\" [expected: \"%s\"]",
		       line, category, locale, result, expected);
	}

	if (strcmp(expected, result) != 0)
		errx(1, "[%d] setlocale(%d, \"%s\")=\"%s\" [expected: \"%s\"]",
		       line, category, locale, result, expected);
}

static void
_test_MB_CUR_MAX(size_t expected, int line)
{
	if (MB_CUR_MAX != expected)
		errx(1, "[%d] MB_CUR_MAX=%ld [expected %ld]", 
			line, MB_CUR_MAX, expected);
}

static void
_test_isalpha(int expected, int c, int line)
{
	int result = isalpha(c);
	if (!!result != expected)
		errx(1, "[%d] isalpha(%d)=%d [expected %d]",
			line, c, result, expected);
}

int
main(int argc, char *argv[])
{
	/* check initial state (should be "C") */
	test_setlocale("C", LC_ALL, NULL); /* check */
	test_MB_CUR_MAX(1);
	test_isalpha(0, 0xe9); /* iso-8859-1 eacute */

	/* load from env */
	/* NOTE: we don't support non-C locales for some categories */
	test_setlocale("fr_FR.UTF-8", LC_CTYPE, ""); /* set */
	test_setlocale("fr_FR.UTF-8", LC_MESSAGES, ""); /* set */
	test_MB_CUR_MAX(4);
	test_isalpha(0, 0xe9); /* iso-8859-1 eacute */

	test_setlocale("C", LC_MESSAGES, "C"); /* set */
	test_MB_CUR_MAX(4);
	test_setlocale("C/fr_FR.UTF-8/C/C/C/C", LC_ALL, NULL); /* check */

	test_setlocale("C", LC_CTYPE, "C"); /* set */
	test_MB_CUR_MAX(1);
	test_setlocale("C", LC_ALL, NULL); /* check */

	/* check for errors on checking */
	test_setlocale("C", LC_ALL, "C"); /* reset */
	test_setlocale(NULL, -1, NULL);
	test_setlocale(NULL, _LC_LAST, NULL);
	test_setlocale(NULL, _LC_LAST+0xff, NULL);
	test_setlocale("C", LC_ALL, NULL); /* check */

	/* check for errors on setting */
	test_setlocale(NULL, -1, "");
	test_setlocale(NULL, _LC_LAST, "");
	test_setlocale(NULL, _LC_LAST+0xff, "");
	test_setlocale("C", LC_ALL, NULL); /* check */

	/* no codeset, fallback to ASCII */
	test_setlocale("C", LC_ALL, "C"); /* reset */
	test_setlocale("invalid", LC_CTYPE, "invalid"); /* set */
	test_setlocale("invalid", LC_CTYPE, NULL);
	test_MB_CUR_MAX(1);
	test_isalpha(0, 0xe9); /* iso-8859-1 eacute */

	/* with codeset */
	test_setlocale("C", LC_ALL, "C"); /* reset */
	test_setlocale("invalid.UTF-8", LC_CTYPE, "invalid.UTF-8"); /* set */
	test_setlocale("invalid.UTF-8", LC_CTYPE, NULL);
	test_setlocale("C/invalid.UTF-8/C/C/C/C", LC_ALL, NULL);
	test_MB_CUR_MAX(4);

	/* with invalid codeset (is an error) */
	test_setlocale("C", LC_ALL, "C"); /* reset */
	test_setlocale(NULL, LC_CTYPE, "fr_FR.invalid"); /* set */
	test_setlocale("C", LC_CTYPE, NULL);
	test_MB_CUR_MAX(1);

	return (EXIT_SUCCESS);
}
