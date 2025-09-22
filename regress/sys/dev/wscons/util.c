/*	$OpenBSD: util.c,v 1.1 2018/12/17 19:26:25 anton Exp $	*/

/*
 * Copyright (c) 2018 Anton Lindqvist <anton@openbsd.org>
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

#include <err.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "util.h"

static __dead void usage(void);

int
dotest(int argc, char *argv[], const struct test *tests)
{
	const struct test *test;
	const char *dev = NULL;
	int c, fd;

	while ((c = getopt(argc, argv, "d:")) != -1)
		switch (c) {
		case 'd':
			dev = optarg;
			break;
		default:
			usage();
		}
	argc -= optind;
	argv += optind;
	if (dev == NULL || argc != 1)
		usage();

	fd = open(dev, O_RDWR);
	if (fd == -1)
		err(1, "open: %s", dev);

	for (test = tests; test->t_name != NULL; test++) {
		if (strcmp(argv[0], test->t_name) == 0)
			break;
	}
	if (test->t_name == NULL)
		errx(1, "%s: no such test", argv[0]);

	return test->t_func(fd);
}

static __dead void
usage(void)
{
	fprintf(stderr, "usage: %s -d device test\n", getprogname());
	exit(1);
}
