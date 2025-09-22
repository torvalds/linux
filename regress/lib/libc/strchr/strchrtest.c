/*	$OpenBSD: strchrtest.c,v 1.2 2021/09/01 09:26:32 jasper Exp $	*/

/*
 * Copyright (c) 2021 Visa Hankala
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

int
main(void)
{
	char *buf;
	size_t bufsize;

	/* Allocate buffer with guard pages. */
	bufsize = getpagesize();
	buf = malloc(bufsize);
	if (buf == NULL) {
		fprintf(stderr, "unable to allocate memory\n");
		return 1;
	}

	memset(buf, 0, bufsize);
	assert(strchr(buf, 'a') == NULL);
	assert(strchr(buf, '\0') == buf);
	assert(strrchr(buf, 'a') == NULL);
	assert(strrchr(buf, '\0') == buf);

	memcpy(buf, "haystack\xcf\x80", 10);
	assert(strchr(buf, 'a') == buf + 1);
	assert(strchr(buf, '\x80') == buf + 9);
	assert(strchr(buf, 0x180) == buf + 9);
	assert(strchr(buf, '\0') == buf + 10);
	assert(strrchr(buf, 'a') == buf + 5);
	assert(strrchr(buf, '\xcf') == buf + 8);
	assert(strrchr(buf, 0x3cf) == buf + 8);
	assert(strrchr(buf, '\0') == buf + 10);

	memset(buf, 'a', bufsize - 2);
	buf[0] = 'b';
	buf[bufsize - 2] = 'b';
	assert(strchr(buf, 'b') == buf);
	assert(strchr(buf, 'c') == NULL);
	assert(strchr(buf, '\0') == buf + bufsize - 1);
	assert(strrchr(buf, 'b') == buf + bufsize - 2);
	assert(strrchr(buf, 'c') == NULL);
	assert(strrchr(buf, '\0') == buf + bufsize - 1);

	return 0;
}
