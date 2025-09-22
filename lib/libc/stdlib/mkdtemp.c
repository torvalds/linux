/*	$OpenBSD: mkdtemp.c,v 1.2 2024/03/01 21:30:40 millert Exp $ */
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
#include <stdlib.h>

static int
mkdtemp_cb(const char *path, int flags)
{
	return mkdir(path, S_IRUSR|S_IWUSR|S_IXUSR);
}

char *
mkdtemp(char *path)
{
	if (__mktemp4(path, 0, 0, mkdtemp_cb) == 0)
		return path;
	return NULL;
}

char *
mkdtemps(char *path, int slen)
{
	if (__mktemp4(path, slen, 0, mkdtemp_cb) == 0)
		return path;
	return NULL;
}
