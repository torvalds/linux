/*	$OpenBSD: test_mbrtowc.c,v 1.3 2020/03/09 09:29:10 dlg Exp $	*/
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
#include <locale.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>

static mbstate_t	 mbs;

void
onetest(const char *name, const char *in, size_t insz,
    int outerr, size_t outsz, wint_t out)
{
	wchar_t		 wc;
	size_t		 sz;

	sz = mbrtowc(&wc, in, insz, &mbs);
	if (errno != outerr)
		err(1, "%zu %s(%zd)", MB_CUR_MAX, name, insz);
	if (sz != outsz || (out != WEOF && wc != out))
		errx(1, "%zu %s(%zd) = (%zd, %d) != (%zd, %d)",
		    MB_CUR_MAX, name, insz, sz, wc, outsz, out);
	if (mbsinit(&mbs) == (insz && outsz == (size_t)-2))
		errx(1, "%zu %s(%zd) mbsinit", MB_CUR_MAX, name, insz);
	if (errno == 0 && outerr == 0)
		return;
	errno = 0;
	memset(&mbs, 0, sizeof(mbs));
}

int
main(void)
{
	onetest("NUL", "", 0, 0, -2, WEOF);
	onetest("NUL", "", 2, 0, 0, L'\0');
	onetest("BEL", "\a", 2, 0, 1, L'\a');
	onetest("A", "A", 2, 0, 1, L'A');
	onetest("DEL", "\177", 2, 0, 1, L'\177');
	onetest("CSI", "\233", 2, 0, 1, L'\233');

	if (setlocale(LC_CTYPE, "en_US.UTF-8") == NULL)
		errx(1, "setlocale(UTF-8) failed");

	onetest("NUL", "", 0, 0, -2, WEOF);
	onetest("NUL", "", 8, 0, 0, L'\0');
	onetest("BEL", "\a", 8, 0, 1, L'\a');
	onetest("A", "A", 8, 0, 1, L'A');
	onetest("DEL", "\177", 8, 0, 1, L'\177');
	onetest("0x80", "\200", 8, EILSEQ, -1, WEOF);
	onetest("0xc3", "\303", 1, 0, -2, WEOF);
	onetest("U+00E9", "\251", 8, 0, 1, 0xe9);
	onetest("0xec", "\354", 1, 0, -2, WEOF);
	onetest("0xecbf", "\277", 1, 0, -2, WEOF);
	onetest("U+CFFF", "\277", 8, 0, 1, 0xcfff);

	if (setlocale(LC_CTYPE, "POSIX") == NULL)
		errx(1, "setlocale(POSIX) failed");

	onetest("0xff", "\277", 2, 0, 1, L'\277');

	if (setlocale(LC_CTYPE, "en_US.UTF-8") == NULL)
		errx(1, "second setlocale(UTF-8) failed");

	onetest("U+13000", "\360\223\200\200", 8, 0, 4, 0x13000);

	return 0;
}
