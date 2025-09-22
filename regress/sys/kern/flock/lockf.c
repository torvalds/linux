/*	$OpenBSD: lockf.c,v 1.1 2018/11/06 18:11:11 anton Exp $	*/

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
#include <unistd.h>

#include "util.h"

int verbose = 0;

/*
 * Test 1 - F_LOCK positive length
 */
static int
test1(int fd)
{
	struct flock fl;
	pid_t pid;
	int res;

	fl.l_start = 0;
	fl.l_len = 16;
	fl.l_type = F_WRLCK;
	fl.l_whence = SEEK_CUR;
	res = fcntl(fd, F_SETLK, &fl);
	FAIL(res != 0);

	pid = fork();
	if (pid == -1)
		err(1, "fork");
	if (pid == 0) {
		res = fcntl(fd, F_SETLK, &fl);
		FAIL(res != -1);
		FAIL(fl.l_type != F_WRLCK);
		_exit(0);
	}
	res = safe_waitpid(pid);
	FAIL(res != 0);

	SUCCEED;
}

/*
 * Test 2 - F_LOCK negative length
 */
static int
test2(int fd)
{
	struct flock fl;
	pid_t pid;
	off_t len = 16;
	int res;

	if (lseek(fd, len, SEEK_SET) == -1)
		err(1, "lseek");

	fl.l_start = 0;
	fl.l_len = -len;
	fl.l_type = F_WRLCK;
	fl.l_whence = SEEK_CUR;
	res = fcntl(fd, F_SETLK, &fl);
	FAIL(res != 0);

	pid = fork();
	if (pid == -1)
		err(1, "fork");
	if (pid == 0) {
		if (lseek(fd, 0, SEEK_SET) == -1)
			err(1, "lseek");

		fl.l_start = 0;
		fl.l_len = len;
		fl.l_whence = SEEK_SET;
		res = fcntl(fd, F_GETLK, &fl);
		FAIL(res != 0);
		FAIL(fl.l_type != F_WRLCK);
		_exit(0);
	}
	res = safe_waitpid(pid);
	FAIL(res != 0);

	SUCCEED;
}

static struct test tests[] = {
	{	test1,		0	},
	{	test2,		0	},
};

static int test_count = sizeof(tests) / sizeof(tests[0]);

int
main(int argc, char *argv[])
{
	const char *errstr;
	int c, error, fd, i;
	int testnum = 0;

	while ((c = getopt(argc, argv, "v")) != -1)
		switch (c) {
		case 'v':
			verbose = 1;
			break;
		default:
			usage();
		}
	argc -= optind;
	argv += optind;
	if (argc > 1)
		usage();
	if (argc == 1) {
		testnum = strtonum(argv[0], 1, test_count, &errstr);
		if (testnum == 0)
			errx(1, "test number %s", errstr);
	}

	fd = make_file(1024);

	error = 0;
	for (i = 0; i < test_count; i++) {
		if (testnum == 0 || i + 1 == testnum)
			error |= tests[i].testfn(fd);
	}

	return (error ? 1 : 0);
}
