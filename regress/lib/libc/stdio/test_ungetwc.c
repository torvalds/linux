/*	$OpenBSD: test_ungetwc.c,v 1.1 2025/05/25 05:32:45 yasuoka Exp $	*/

/*
 * Copyright (c) 2025 YASUOKA Masahiko <yasuoka@yasuoka.net>
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

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <locale.h>
#include <wchar.h>

/* we use assert() */
#undef	NDEBUG

#define	TMPFILENAME	"test_ungetwc.tmp"

void setupw(void);
void test_fflush_ungetwc0(void);

void
setupw(void)
{
	FILE	*fp;

	/* common setup */
	unlink(TMPFILENAME);
	fp = fopen(TMPFILENAME, "w+");
	assert(fp != NULL);
	/* Konnitiwa Sekai(in Kanji) */
	fputws(L"\u3053\u3093\u306b\u3061\u308f \u4e16\u754c\n", fp);
	fclose(fp);
}

/* fflush work with reading file and seekable + ungetwc */
void
test_fflush_ungetwc0(void)
{
	int	 r;
	wchar_t	 buf[80];
	FILE	*fp;

	setupw();

	fp = fopen(TMPFILENAME, "r");

	assert(fp != NULL);
	assert(fgetwc(fp) == L'\u3053');	/* Ko */
	assert(fgetwc(fp) == L'\u3093');	/* N  */
	assert(fgetwc(fp) == L'\u306b');	/* Ni */
	assert(fgetwc(fp) == L'\u3061');	/* Ti */
	assert(fgetwc(fp) == L'\u308f');	/* Wa */

	/* push 263A(smile) back */
	assert(ungetwc(L'\u263a', fp));

	/* we support 1 push back wchar_t */
	assert(fgetwc(fp) == L'\u263a');

	/* can read reset of that */
	fgetws(buf, sizeof(buf), fp);
	assert(wcscmp(buf, L" \u4e16\u754c\n") == 0);

	r = fclose(fp);
	assert(r == 0);
}

int
main(int argc, char *argv[])
{
	setlocale(LC_ALL, "C.UTF-8");

	test_fflush_ungetwc0();

	exit(0);
}
