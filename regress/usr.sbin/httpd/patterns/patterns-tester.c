/* $OpenBSD: patterns-tester.c,v 1.2 2025/05/26 06:18:49 anton Exp $ */
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

#include <stdio.h>
#include <stdlib.h>
#include <err.h>
#include <string.h>

#include "patterns.h"

static void read_string(char *, size_t);
static void read_string_stop(void);

static void
read_string(char *buf, size_t len)
{
	size_t i;

	/* init */
	bzero(buf, len);

	/* read */
	if (fgets(buf, len, stdin) == NULL)
		err(1, "fgets");

	/* strip '\n' */
	i = strnlen(buf, len);
	if (i != 0)
		buf[i-1] = '\0';
}

static void
read_string_stop()
{
	if (getchar() != EOF)
		errx(1, "read_string_stop: too many input");
}

int
main(int argc, char *argv[])
{
	char string[1024];
	char pattern[1024];
	struct str_match m;
	const char *errstr = NULL;
	int ret;
	size_t i;

	/* read testcase */
	if (argc != 3) {
		/* from stdin (useful for afl) */
		read_string(string, sizeof(string));
		read_string(pattern, sizeof(pattern));
		read_string_stop();
	} else {
		/* from arguments */
		strlcpy(string, argv[1], sizeof(string));
		strlcpy(pattern, argv[2], sizeof(pattern));
	}

	/* print testcase */
	printf("string='%s'\n", string);
	printf("pattern='%s'\n", pattern);

	/* test it ! */
	ret = str_match(string, pattern, &m, &errstr);
	if (errstr != NULL)
		errx(1, "str_match: %s", errstr);

	/* print result */
	printf("ret=%d num=%d\n", ret, m.sm_nmatch);
	for (i=0; i<m.sm_nmatch; i++) {
		printf("%ld: %s\n", i, m.sm_match[i]);
	}

	str_match_free(&m);

	return (EXIT_SUCCESS);
}
