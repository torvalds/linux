/*	$OpenBSD: strnlentest.c,v 1.3 2021/09/01 09:26:32 jasper Exp $ */

/*
 * Copyright (c) 2010 Todd C. Miller <millert@openbsd.org>
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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

int main(int argc, char *argv[])
{
	char *buf;
	int failures = 0;
	size_t len, bufsize;

	bufsize = getpagesize(); /* trigger guard pages easily */
	buf = malloc(bufsize);
	if (buf == NULL) {
		fprintf(stderr, "unable to allocate memory\n");
		return 1;
	}
	memset(buf, 'a', bufsize);

	len = strnlen(buf, bufsize);
	if (len != bufsize) {
		fprintf(stderr, "strnlen: failed unterminated buffer test (1)");
		failures++;
	}

	len = strnlen(buf, bufsize / 2);
	if (len != bufsize / 2) {
		fprintf(stderr, "strnlen: failed unterminated buffer test (2)");
		failures++;
	}

	buf[bufsize - 1] = '\0';
	len = strnlen(buf, bufsize);
	if (len != bufsize - 1) {
		fprintf(stderr, "strnlen: failed NUL-terminated buffer test (1)");
		failures++;
	}

	len = strnlen(buf, (size_t)-1);
	if (len != bufsize - 1) {
		fprintf(stderr, "strnlen: failed NUL-terminated buffer test (2)");
		failures++;
	}

	return failures;
}
