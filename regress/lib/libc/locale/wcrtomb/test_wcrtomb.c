/*	$OpenBSD: test_wcrtomb.c,v 1.3 2021/07/03 12:04:53 schwarze Exp $	*/
/*
 * Copyright (c) 2016 Ingo Schwarze <schwarze@openbsd.org>
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

#include <sys/types.h>
#include <err.h>
#include <errno.h>
#include <limits.h>
#include <locale.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>

static mbstate_t	 mbs;

void
onetest(const char *name, const wchar_t wcin, int outerr, const char *out)
{
	char		 buf[MB_LEN_MAX];
	size_t		 sz, outsz;

	memset(buf, 0, MB_LEN_MAX);
	outsz = out == NULL ? (size_t)-1 : *out == '\0' ? 1 : strlen(out);
	sz = wcrtomb(buf, wcin, &mbs);
	if (errno != outerr)
		err(1, "%zu %s U+%04X", MB_CUR_MAX, name, wcin);
	if (sz != outsz || (out != NULL && strncmp(buf, out, sz)))
		errx(1, "%zu %s U+%04X: %4.4s(%zd) != %4.4s(%zd)",
		    MB_CUR_MAX, name, wcin, buf, sz,
		    out == NULL ? "(NULL)" : out, outsz);
	if (mbsinit(&mbs) == 0)
		errx(1, "%zu %s U+%04X mbsinit", MB_CUR_MAX, name, wcin);
	if (errno == 0 && outerr == 0)
		return;
	errno = 0;
	memset(&mbs, 0, sizeof(mbs));
}

int
main(void)
{
	onetest("NUL", L'\0', 0, "");
	onetest("BEL", L'\a', 0, "\a");
	onetest("A", L'A', 0, "A");
	onetest("DEL", L'\177', 0, "\177");
	onetest("CSI", L'\233', 0, "\233");
	onetest("0x100", 0x100, EILSEQ, NULL);

	if (setlocale(LC_CTYPE, "en_US.UTF-8") == NULL)
		errx(1, "setlocale(UTF-8) failed");

	onetest("NUL", L'\0', 0, "");
	onetest("BEL", L'\a', 0, "\a");
	onetest("A", L'A', 0, "A");
	onetest("DEL", L'\177', 0, "\177");
	onetest("CSI", L'\233', 0, "\302\233");
	onetest("0xe9", 0xe9, 0, "\303\251");
	onetest("0xcfff", 0xcfff, 0, "\354\277\277");
	onetest("0xd800", 0xd800, EILSEQ, NULL);

	if (setlocale(LC_CTYPE, "POSIX") == NULL)
		errx(1, "setlocale(POSIX) failed");

	onetest("0xff", L'\377', 0, "\377");

	if (setlocale(LC_CTYPE, "en_US.UTF-8") == NULL)
		errx(1, "second setlocale(UTF-8) failed");

	onetest("U+13000", 0x13000, 0, "\360\223\200\200");

	return 0;
}
