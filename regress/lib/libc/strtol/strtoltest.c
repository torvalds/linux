/*	$OpenBSD: strtoltest.c,v 1.4 2017/07/15 17:08:26 jsing Exp $	*/
/*
 * Copyright (c) 2012 Joel Sing <jsing@openbsd.org>
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

#include <errno.h>
#include <limits.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

struct strtol_test {
	char	*input;
	long	output;
	char	end;
	int	base;
	int	err;
};

struct strtol_test strtol_tests[] = {
	{"1234567890",	1234567890L,	'\0',	0,	0},
	{"0755",	493L,		'\0',	0,	0},
	{"0x7fFFffFf",	2147483647L,	'\0',	0,	0},
	{"1234567890",	0L,		'1',	1,	EINVAL},
	{"10101010",	170L,		'\0',	2,	0},
	{"755",		493L,		'\0',	8,	0},
	{"1234567890",	1234567890L,	'\0',	10,	0},
	{"abc",		0L,		'a',	10,	0},
	{"123xyz",	123L,		'x',	10,	0},
	{"-080000000",	-2147483648L,	'\0',	16,	0},
	{"deadbeefdeadbeef", LONG_MAX,	'\0',	16,	ERANGE},
	{"deadzbeef",	57005L,		'z',	16,	0},
	{"0xy",		0L,		'x',	16,	0},
	{"-quitebigmchuge", LONG_MIN,	'\0',	32,	ERANGE},
	{"zzz",		46655L,		'\0',	36,	0},
	{"1234567890",	0L,		'1',	37, 	EINVAL},
	{"1234567890",	0L,		'1',	123,	EINVAL},
};

int
main(int argc, char **argv)
{
	struct strtol_test *test;
	int failure = 0;
	char *end;
	u_int i;
	long n;

	for (i = 0; i < (sizeof(strtol_tests) / sizeof(strtol_tests[0])); i++) {
		test = &strtol_tests[i];
		errno = 0;
		n = strtol(test->input, &end, test->base);
		if (n != test->output) {
			fprintf(stderr, "TEST %i FAILED: %s base %i: %li\n",
			    i, test->input, test->base, n);
			failure = 1;
		} else if (*end != test->end) {
			fprintf(stderr, "TEST %i FAILED: end is not %c: %c\n",
			    i, test->end, *end);
			failure = 1;
		} else if (errno != test->err) {
			fprintf(stderr, "TEST %i FAILED: errno is not %i: %i\n",
			    i, test->err, errno);
			failure = 1;
		}
	}

	return failure;
}
