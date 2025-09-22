/*	$OpenBSD: open_memstream.c,v 1.10 2023/07/11 12:14:16 claudio Exp $	*/

/*
 * Copyright (c) 2011 Martin Pieuchot <mpi@openbsd.org>
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
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "local.h"

#define	MINIMUM(a, b)	(((a) < (b)) ? (a) : (b))

struct state {
	char		 *string;	/* actual stream */
	char		**pbuf;		/* point to the stream */
	size_t		 *psize;	/* point to min(pos, len) */
	size_t		  pos;		/* current position */
	size_t		  size;		/* number of allocated char */
	size_t		  len;		/* length of the data */
};

static int
memstream_write(void *v, const char *b, int l)
{
	struct state	*st = v;
	char		*p;
	size_t		 i, end;

	end = (st->pos + l);

	if (end >= st->size) {
		/* 1.6 is (very) close to the golden ratio. */
		size_t	sz = st->size * 8 / 5;

		if (sz < end + 1)
			sz = end + 1;
		p = recallocarray(st->string, st->size, sz, 1);
		if (!p)
			return (-1);
		*st->pbuf = st->string = p;
		st->size = sz;
	}

	for (i = 0; i < l; i++)
		st->string[st->pos + i] = b[i];
	st->pos += l;

	if (st->pos > st->len) {
		st->len = st->pos;
		st->string[st->len] = '\0';
	}

	*st->psize = st->pos;

	return (i);
}

static fpos_t
memstream_seek(void *v, fpos_t off, int whence)
{
	struct state	*st = v;
	size_t		 base = 0;

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

	if ((off > 0 && off > SIZE_MAX - base) || (off < 0 && base < -off)) {
		errno = EOVERFLOW;
		return (-1);
	}

	st->pos = base + off;
	*st->psize = MINIMUM(st->pos, st->len);

	return (st->pos);
}

static int
memstream_close(void *v)
{
	struct state	*st = v;

	free(st);

	return (0);
}

FILE *
open_memstream(char **pbuf, size_t *psize)
{
	struct state	*st;
	FILE		*fp;

	if (pbuf == NULL || psize == NULL) {
		errno = EINVAL;
		return (NULL);
	}

	if ((st = malloc(sizeof(*st))) == NULL)
		return (NULL);

	if ((fp = __sfp()) == NULL) {
		free(st);
		return (NULL);
	}

	st->size = BUFSIZ;
	if ((st->string = calloc(1, st->size)) == NULL) {
		free(st);
		fp->_flags = 0;
		return (NULL);
	}

	st->pos = 0;
	st->len = 0;
	st->pbuf = pbuf;
	st->psize = psize;

	*pbuf = st->string;
	*psize = st->len;

	fp->_flags = __SWR;
	fp->_file = -1;
	fp->_cookie = st;
	fp->_read = NULL;
	fp->_write = memstream_write;
	fp->_seek = memstream_seek;
	fp->_close = memstream_close;
	_SET_ORIENTATION(fp, -1);

	return (fp);
}
DEF_WEAK(open_memstream);
