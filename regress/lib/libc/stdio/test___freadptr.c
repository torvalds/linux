/*	$OpenBSD: test___freadptr.c,v 1.1 2025/05/25 00:20:54 yasuoka Exp $	*/

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

#include <sys/types.h>
#include <assert.h>
#include <stdio.h>
#include <stdio_ext.h>
#include <stdlib.h>
#include <string.h>

/* we use assert() */
#undef	NDEBUG

#define	TMPFILENAME	"test___freadptr.tmp"

void test___freadptr0(void);

/* test __freadptr() and __freadptrinc() */
void
test___freadptr0(void)
{
	FILE		*fp;
	int		 r;
	ssize_t		 s;
	const char	*p;

	fp = fopen(TMPFILENAME, "w");
	assert(fp != NULL);
	r = fputs("Hello world", fp);
	assert(r >= 0);
	r = fclose(fp);

	fp = fopen(TMPFILENAME, "r");
	assert(fgetc(fp) == 'H');
	p = __freadptr(fp, &s);
	assert(p != NULL);
	assert(s > 4);		/* this test assume this (not by the spec) */
	assert(*p == 'e');
	assert(strncmp(p, "ello world", s) == 0);

	__freadptrinc(fp, 4);
	assert(fgetc(fp) == ' ');

	ungetc('A', fp);
	ungetc('A', fp);
	ungetc('A', fp);
	p = __freadptr(fp, &s);
	assert(s > 0);
	assert(*p == 'A');
	/* ptr will contains only the pushback buffer */
	assert(strncmp(p, "AAAworld", s) == 0);

	r = fclose(fp);
	assert(r == 0);
}

int
main(int argc, char *argv[])
{
	test___freadptr0();

	exit(0);
}
