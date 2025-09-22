/*	$OpenBSD: atf-c.c,v 1.2 2019/11/22 15:59:53 bluhm Exp $	*/
/*
 * Copyright (c) 2019 Moritz Buhl <openbsd@moritzbuhl.de>
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

#include <sys/wait.h>

#include <err.h>
#include <errno.h>
#include <limits.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <pwd.h>
#include <unistd.h>

#include "atf-c.h"

void usage(void);

int cleanup;
int count;
int inspect;
int run;
int test;

int
main(int argc, char *argv[])
{
	int ch, test;
	const char *errstr, *num;

	while ((ch = getopt(argc, argv, "c:i:nr:")) != -1) {
		switch(ch) {
		case 'c':
			cleanup = 1;
			num = optarg;
			break;
		case 'i':
			inspect = 1;
			num = optarg;
			break;
		case 'n':
			count = 1;
			break;
		case 'r':
			run = 1;
			num = optarg;
			break;
		default:
			usage();
		}
	}
	argc -= optind;
	argv += optind;

	if (cleanup + count + inspect + run > 1)
		usage();

	if (cleanup || inspect || run) {
		test = strtonum(num, 1, INT_MAX, &errstr);
		if (errstr != NULL)
			errx(1, "test # is %s: %s", errstr, argv[1]);
	}
	if (count)
		printf("%d\n", atf_test(0, 0));
	else if (cleanup)
		ATF_CLEANUP(test);
	else if (run)
		ATF_RUN(test);
	else if (inspect)
		ATF_INSPECT(test);
	else
		usage();

	return 0;
}

void
usage(void)
{
	fprintf(stderr, "usage: %s [-n] [-c|i|r test#]\n", getprogname());
	exit(1);
}

void
atf_require(int exp, int expected_errno, const char *expstr, const char *src,
    const int lineno, char *fmt, ...)
{
	va_list args;
	if (!(exp)) {
		fprintf(stderr, "\n%s:%d: ", src, lineno);
		if (fmt != NULL) {
			va_start(args, fmt);
			vfprintf(stderr, fmt, args);
			va_end(args);
		} else {
			fprintf(stderr, "'%s' evaluated to false\n", expstr);
		}
		exit(1);
	} else if (expected_errno >= 0 && errno != expected_errno) {
		fprintf(stderr, "\n%s:%d: ", src, lineno);
		fprintf(stderr, "expected errno %d but got %d instead\n",
		    expected_errno, errno);
		exit(1);
	}
	return;
}

void
atf_tc_fail(char *fmt, ...)
{
	va_list args;
	va_start(args, fmt);
	verrx(1, fmt, args);
}
