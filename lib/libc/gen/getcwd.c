/*	$OpenBSD: getcwd.c,v 1.22 2023/05/18 16:11:09 guenther Exp $	*/

/*
 * Copyright (c) 2005 Marius Eriksen <marius@openbsd.org>
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
#include <unistd.h>

char *
getcwd(char *buf, size_t size)
{
	char *allocated = NULL;

	if (buf != NULL && size == 0) {
		errno = EINVAL;
		return (NULL);
	}

	if (buf == NULL &&
	    (allocated = buf = malloc(size = PATH_MAX)) == NULL)
		return (NULL);

	if (__getcwd(buf, size) == -1) {
		free(allocated);
		return (NULL);
	}

	return (buf);
}
DEF_WEAK(getcwd);
