/*	$OpenBSD: util.c,v 1.1 2018/11/06 18:11:11 anton Exp $	*/

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

#include <sys/wait.h>

#include <err.h>
#include <errno.h>
#include <limits.h>
#include <paths.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "util.h"

static const char *tmpdir(void);

int
make_file(off_t size)
{
	char template[PATH_MAX];
	const char *dir;
	int fd, n;

	dir = tmpdir();
	n = snprintf(template, sizeof(template), "%sflock.XXXXXX", dir);
	if (n == -1 || n >= (int)sizeof(template))
		errc(1, ENAMETOOLONG, "%s", __func__);

	fd = mkstemp(template);
	if (fd == -1)
		err(1, "mkstemp");
	if (ftruncate(fd, size) == -1)
		err(1, "ftruncate");
	if (unlink(template) == -1)
		err(1, "unlink");

	return (fd);
}

int
safe_waitpid(pid_t pid)
{
	int save_errno;
	int status;

	save_errno = errno;
	errno = 0;
	while (waitpid(pid, &status, 0) != pid) {
		if (errno == EINTR)
			continue;
		err(1, "waitpid");
	}
	errno = save_errno;

	return (status);
}

__dead void
usage(void)
{
	fprintf(stderr, "usage: %s [-v] testnum\n", getprogname());
	exit(1);
}

static const char *
tmpdir(void)
{
	static char path[PATH_MAX];
	const char *p;
	size_t len;
	int n;
	int nosuffix = 1;

	p = getenv("TMPDIR");
	if (p == NULL || *p == '\0')
		p = _PATH_TMP;
	len = strlen(p);
	if (len == 0)
		errx(1, "%s: empty path", __func__);
	if (p[len - 1] == '/')
		nosuffix = 0;
	n = snprintf(path, sizeof(path), "%s%s", p, nosuffix ? "/" : "");
	if (n == -1 || n >= (int)sizeof(path))
		errc(1, ENAMETOOLONG, "%s", __func__);

	return (path);
}
