/*	$OpenBSD: mktemp.c,v 1.2 2024/01/19 19:45:02 millert Exp $ */
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
#include <stdlib.h>

static int
mktemp_cb(const char *path, int flags)
{
	struct stat sb;

	if (lstat(path, &sb) == 0)
		errno = EEXIST;
	return (errno == ENOENT ? 0 : -1);
}

/* Also called via tmpnam(3) and tempnam(3). */
char *
_mktemp(char *path)
{
	if (__mktemp4(path, 0, 0, mktemp_cb) == 0)
		return path;
	return NULL;
}

__warn_references(mktemp,
    "mktemp() possibly used unsafely; consider using mkstemp()");

char *
mktemp(char *path)
{
	return _mktemp(path);
}
