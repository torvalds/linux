/*	$OpenBSD: test___freadahead.c,v 1.2 2025/06/03 14:35:27 yasuoka Exp $	*/

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
#include <errno.h>
#include <stdio.h>
#include <stdio_ext.h>
#include <stdlib.h>

/* we use assert() */
#undef	NDEBUG

#define	TMPFILENAME	"test___freadahead.tmp"

void test___freadahead0(void);

void
test___freadahead0(void)
{
	FILE	*fp;
	int	 r;
	size_t	 s;

	fp = fopen(TMPFILENAME, "w");
	assert(fp != NULL);
	r = fputs("Hello world", fp);
	assert(r >= 0);
	r = fclose(fp);

	fp = fopen(TMPFILENAME, "r");
	s = __freadahead(fp);
	assert(s == 0);
	assert(fgetc(fp) == 'H');
	s = __freadahead(fp);
	assert(s == 10);
	r = fflush(fp);
#if 0
	/* fflush() to reading file is not supported (yet) */
	assert(errno == EBADF);
#else
	assert(r == 0);
	s = __freadahead(fp);
	assert(s == 0);
#endif

	r = fclose(fp);
	assert(r == 0);
}

int
main(int argc, char *argv[])
{
	test___freadahead0();

	exit(0);
}
