/*	$OpenBSD: mkstemp.c,v 1.2 2025/08/04 04:59:31 guenther Exp $ */
/*
 * Copyright (c) 2024 Todd C. Miller
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
#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>

#define MKOSTEMP_FLAGS \
	(O_APPEND | O_CLOEXEC | O_CLOFORK | O_DSYNC | O_RSYNC | O_SYNC)

static int
mkstemp_cb(const char *path, int flags)
{
	flags |= O_CREAT | O_EXCL | O_RDWR;
	return open(path, flags, S_IRUSR|S_IWUSR);
}

int
mkostemps(char *path, int slen, int flags)
{
	if (flags & ~MKOSTEMP_FLAGS) {
		errno = EINVAL;
		return -1;
	}
	return __mktemp4(path, slen, flags, mkstemp_cb);
}

int
mkostemp(char *path, int flags)
{
	if (flags & ~MKOSTEMP_FLAGS) {
		errno = EINVAL;
		return -1;
	}
	return __mktemp4(path, 0, flags, mkstemp_cb);
}
DEF_WEAK(mkostemp);

int
mkstemp(char *path)
{
	return __mktemp4(path, 0, 0, mkstemp_cb);
}
DEF_WEAK(mkstemp);

int
mkstemps(char *path, int slen)
{
	return __mktemp4(path, slen, 0, mkstemp_cb);
}
