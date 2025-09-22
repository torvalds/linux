/*	$OpenBSD: test___fseterr.c,v 1.1 2025/05/25 00:20:54 yasuoka Exp $	*/

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
#include <stdio_ext.h>
#include <stdlib.h>

/* we use assert() */
#undef	NDEBUG

#define	TMPFILENAME	"test___fseterr.tmp"

void test___fseterr0(void);

void
test___fseterr0(void)
{
	FILE	*fp;
	int	 r;

	fp = fopen(TMPFILENAME, "w+");
	assert(fp != NULL);

	assert(!ferror(fp));

	r = fprintf(fp, "hello world\n");
	assert(r > 0);

	__fseterr(fp);
	assert(ferror(fp));

	r = fprintf(fp, "hello world\n");
	assert(r == -1);

	fclose(fp);
}

int
main(int argc, char *argv[])
{
	test___fseterr0();

	exit(0);
}
