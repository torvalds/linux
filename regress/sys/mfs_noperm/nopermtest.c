/*	$OpenBSD: nopermtest.c,v 1.1 2018/12/23 11:23:21 natano Exp $	*/

/*
 * Copyright (c) 2018 Martin Natano <natano@natano.net>
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

#include <sys/stat.h>
#include <sys/time.h>

#include <err.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>

#define	EXPECT_OK(expr) do {						\
	int r = (expr);							\
	if (r == -1) {							\
		fprintf(stderr, "FAIL:%d: %s -> r=%d errno=%d(%s)\n",	\
		    __LINE__, #expr, r, errno, strerror(errno));	\
		nfail++;						\
	}								\
} while (0)

#define	EXPECT_ERRNO(expr, expected_errno) do {				\
	int r = (expr);							\
	if (r != -1 || errno != expected_errno) {			\
		fprintf(stderr, "FAIL:%d: %s -> r=%d errno=%d(%s)\n",	\
		    __LINE__, #expr, r, errno, strerror(errno));	\
		nfail++;						\
	}								\
} while (0)

static void	check_locked(const char *);
static void	check_unlocked(const char *);
static void	check_unlocked_vroot(void);
static void	check_unlocked_subdir(void);

__dead static void	usage(void);

static int nfail;

int
main(int argc, char **argv)
{
	const char *mnt, *stage;
	const char *errstr;

	if (argc != 3)
		usage();

	mnt = argv[1];
	stage = argv[2];

	if (strcmp(stage, "locked") == 0)
		check_locked(mnt);
	else if (strcmp(stage, "unlocked") == 0)
		check_unlocked(mnt);
	else
		usage();

	return (nfail > 0);
}

static void
check_locked(const char *mnt)
{
	char path[PATH_MAX];

	EXPECT_OK(access(mnt, F_OK));
	EXPECT_ERRNO(access(mnt, R_OK), EACCES);
	EXPECT_ERRNO(access(mnt, W_OK), EACCES);
	EXPECT_ERRNO(access(mnt, X_OK), EACCES);

	(void)snprintf(path, PATH_MAX, "%s/stdin", mnt);
	EXPECT_ERRNO(mknod(path, S_IFCHR | 0700, makedev(22, 0)), EACCES);

	EXPECT_ERRNO(chown(mnt, getuid(), -1), EPERM);
	EXPECT_ERRNO(chmod(mnt, 0700), EPERM);
	EXPECT_ERRNO(chflags(mnt, SF_ARCHIVED), EPERM);
	EXPECT_ERRNO(utimes(mnt, NULL), EACCES);
}

static void
check_unlocked(const char *mnt)
{
	if (chdir(mnt) == -1)
		err(1, "chdir");

	check_unlocked_vroot();
	check_unlocked_subdir();
}

static void
check_unlocked_vroot(void)
{
	int fd;

	EXPECT_OK(access(".", R_OK | W_OK | X_OK));

	EXPECT_ERRNO(mknod("stdin", S_IFCHR | 0700, makedev(22, 0)), EPERM);

	EXPECT_ERRNO(chown(".", 0, -1), EPERM);
	EXPECT_OK(chmod(".", 0700));
	EXPECT_ERRNO(chflags(".", SF_ARCHIVED), EPERM);
	EXPECT_OK(utimes(".", NULL));
}

static void
check_unlocked_subdir(void)
{
	if (mkdir("sub", 0000) == -1)
		err(1, "mkdir");

	EXPECT_OK(access("sub", R_OK | W_OK | X_OK));

	EXPECT_OK(mknod("sub/stdin", S_IFCHR | 0700, makedev(22, 0)));

	EXPECT_OK(chown("sub", 0, -1));
	EXPECT_OK(chmod("sub", 0000));
	EXPECT_OK(chflags("sub", SF_ARCHIVED));
	EXPECT_OK(utimes("sub", NULL));

	EXPECT_OK(chmod("sub", S_ISVTX | 0700));
	EXPECT_OK(chown("sub/stdin", 0, -1));
	EXPECT_OK(rename("sub/stdin", "sub/stdin2"));
	EXPECT_OK(unlink("sub/stdin2"));
}

__dead static void
usage(void)
{
	(void)fprintf(stderr, "usage: %s mnt stage\n", getprogname());
	exit(1);
}
