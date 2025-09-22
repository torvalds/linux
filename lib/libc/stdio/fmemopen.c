/*	$OpenBSD: fmemopen.c,v 1.5 2020/08/17 16:17:39 millert Exp $	*/

/*
 * Copyright (c) 2011 Martin Pieuchot <mpi@openbsd.org>
 * Copyright (c) 2009 Ted Unangst
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
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "local.h"

struct state {
	char		*string;	/* actual stream */
	size_t		 pos;		/* current position */
	size_t		 size;		/* allocated size */
	size_t		 len;		/* length of the data */
	int		 update;	/* open for update */
	int		 append;	/* open for append */
};

static int
fmemopen_read(void *v, char *b, int l)
{
	struct state	*st = v;
	int		 i;

	for (i = 0; i < l && i + st->pos < st->len; i++)
		b[i] = st->string[st->pos + i];
	st->pos += i;

	return (i);
}

static int
fmemopen_write(void *v, const char *b, int l)
{
	struct state	*st = v;
	int		i;

	if (st->append)
		st->pos = st->len;

	for (i = 0; i < l && i + st->pos < st->size; i++)
		st->string[st->pos + i] = b[i];
	st->pos += i;

	if (st->pos >= st->len) {
		st->len = st->pos;

		if (st->len < st->size)
			st->string[st->len] = '\0';
		else if (!st->update)
			st->string[st->size - 1] = '\0';
	}

	return (i);
}

static fpos_t
fmemopen_seek(void *v, fpos_t off, int whence)
{
	struct state	*st = v;
	ssize_t		 base = 0;

	switch (whence) {
	case SEEK_SET:
		break;
	case SEEK_CUR:
		base = st->pos;
		break;
	case SEEK_END:
		base = st->len;
		break;
	}

	if (off > st->size - base || off < -base) {
		errno = EOVERFLOW;
		return (-1);
	}

	st->pos = base + off;

	return (st->pos);
}

static int
fmemopen_close(void *v)
{
	free(v);

	return (0);
}

static int
fmemopen_close_free(void *v)
{
	struct state	*st = v;

	free(st->string);
	free(st);

	return (0);
}

FILE *
fmemopen(void *buf, size_t size, const char *mode)
{
	struct state	*st;
	FILE		*fp;
	int		 flags, oflags;

	if (size == 0) {
		errno = EINVAL;
		return (NULL);
	}

	if ((flags = __sflags(mode, &oflags)) == 0) {
		errno = EINVAL;
		return (NULL);
	}

	if (buf == NULL && ((oflags & O_RDWR) == 0)) {
		errno = EINVAL;
		return (NULL);
	}

	if ((st = malloc(sizeof(*st))) == NULL)
		return (NULL);

	if ((fp = __sfp()) == NULL) {
		free(st);
		return (NULL);
	}

	st->pos = 0;
	st->len = (oflags & O_TRUNC) ? 0 : size;
	st->size = size;
	st->update = oflags & O_RDWR;
	st->append = oflags & O_APPEND;

	if (buf == NULL) {
		if ((st->string = malloc(size)) == NULL) {
			free(st);
			fp->_flags = 0;
			return (NULL);
		}
		*st->string = '\0';
	} else {
		st->string = (char *)buf;

		if (oflags & O_TRUNC)
			*st->string = '\0';

		if (oflags & O_APPEND) {
			char	*p;

			if ((p = memchr(st->string, '\0', size)) != NULL)
				st->pos = st->len = (p - st->string);
			else
				st->pos = st->len = size;
		}
	}

	fp->_flags = (short)flags;
	fp->_file = -1;
	fp->_cookie = st;
	fp->_read = (flags & __SWR) ? NULL : fmemopen_read;
	fp->_write = (flags & __SRD) ? NULL : fmemopen_write;
	fp->_seek = fmemopen_seek;
	fp->_close = (buf == NULL) ? fmemopen_close_free : fmemopen_close;

	return (fp);
}
DEF_WEAK(fmemopen);
