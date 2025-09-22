/* $OpenBSD: shm_open.c,v 1.10 2025/08/04 04:59:31 guenther Exp $ */
/*
 * Copyright (c) 2013 Ted Unangst <tedu@openbsd.org>
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
#include <sys/mman.h>
#include <sys/stat.h>

#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <sha2.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/* SHA256_DIGEST_STRING_LENGTH includes nul */
/* "/tmp/" + sha256 + ".shm" */
#define SHM_PATH_SIZE (5 + SHA256_DIGEST_STRING_LENGTH + 4)

/* O_CLOEXEC, O_CLOFORK, and O_NOFOLLOW are extensions to POSIX */
#define OK_FLAGS \
	(O_CREAT | O_EXCL | O_TRUNC | O_CLOEXEC | O_CLOFORK | O_NOFOLLOW)

static void
makeshmpath(const char *origpath, char *shmpath, size_t len)
{
	char buf[SHA256_DIGEST_STRING_LENGTH];

	SHA256Data(origpath, strlen(origpath), buf);
	snprintf(shmpath, len, "/tmp/%s.shm", buf);
}

int
shm_open(const char *path, int flags, mode_t mode)
{
	char shmpath[SHM_PATH_SIZE];
	struct stat sb;
	int fd;

	if (((flags & O_ACCMODE) != O_RDONLY && (flags & O_ACCMODE) != O_RDWR)
	    || (flags & ~(O_ACCMODE | OK_FLAGS))) {
		errno = EINVAL;
		return -1;
	}

	flags |= O_CLOEXEC | O_NOFOLLOW;
	mode = mode & 0600;

	makeshmpath(path, shmpath, sizeof(shmpath));

	fd = HIDDEN(open)(shmpath, flags, mode);
	if (fd == -1)
		return -1;
	if (fstat(fd, &sb) == -1 || !S_ISREG(sb.st_mode)) {
		HIDDEN(close)(fd);
		errno = EINVAL;
		return -1;
	}
	if (sb.st_uid != geteuid()) {
		HIDDEN(close)(fd);
		errno = EPERM;
		return -1;
	}
	return fd;
}
DEF_WEAK(shm_open);

int
shm_unlink(const char *path)
{
	char shmpath[SHM_PATH_SIZE];

	makeshmpath(path, shmpath, sizeof(shmpath));
	return unlink(shmpath);
}

int
shm_mkstemp(char *template)
{
	size_t templatelen;
	char *t;
	int i, fd;

	templatelen = strlen(template);
	t = malloc(templatelen + 1);
	if (!t)
		return -1;
	t[templatelen] = '\0';

	fd = -1;
	for (i = 0; i < INT_MAX; i++) {
		memcpy(t, template, templatelen);
		if (!_mktemp(t))
			break;
		fd = shm_open(t, O_RDWR | O_EXCL | O_CREAT, 0600);
		if (fd != -1) {
			memcpy(template, t, templatelen);
			break;
		}
	}

	free(t);
	return fd;
}

