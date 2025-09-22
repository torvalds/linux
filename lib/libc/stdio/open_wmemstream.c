/*	$OpenBSD: open_wmemstream.c,v 1.10 2023/07/11 12:14:16 claudio Exp $	*/

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
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#include "local.h"

#define	MINIMUM(a, b)	(((a) < (b)) ? (a) : (b))

struct state {
	wchar_t		 *string;	/* actual stream */
	wchar_t		**pbuf;		/* point to the stream */
	size_t		 *psize;	/* point to min(pos, len) */
	size_t		  pos;		/* current position */
	size_t		  size;		/* number of allocated wchar_t */
	size_t		  len;		/* length of the data */
	mbstate_t	  mbs;		/* conversion state of the stream */
};

static int
wmemstream_write(void *v, const char *b, int l)
{
	struct state	*st = v;
	wchar_t		*p;
	size_t		 nmc, len, end;

	end = (st->pos + l);

	if (end >= st->size) {
		/* 1.6 is (very) close to the golden ratio. */
		size_t	sz = st->size * 8 / 5;

		if (sz < end + 1)
			sz = end + 1;
		p = recallocarray(st->string, st->size, sz, sizeof(wchar_t));
		if (!p)
			return (-1);
		*st->pbuf = st->string = p;
		st->size = sz;
	}

	nmc = (st->size - st->pos) * sizeof(wchar_t);
	len = mbsnrtowcs(st->string + st->pos, &b, nmc, l, &st->mbs);
	if (len == (size_t)-1)
		return (-1);
	st->pos += len;

	if (st->pos > st->len) {
		st->len = st->pos;
		st->string[st->len] = L'\0';
	}

	*st->psize = st->pos;

	return (len);
}

static fpos_t
wmemstream_seek(void *v, fpos_t off, int whence)
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

	if (off > (SIZE_MAX / sizeof(wchar_t)) - base || off < -base) {
		errno = EOVERFLOW;
		return (-1);
	}

	/*
	 * XXX Clearing mbs here invalidates shift state for state-
	 * dependent encodings, but they are not (yet) supported.
	 */
	bzero(&st->mbs, sizeof(st->mbs));

	st->pos = base + off;
	*st->psize = MINIMUM(st->pos, st->len);

	return (st->pos);
}

static int
wmemstream_close(void *v)
{
	struct state	*st = v;

	free(st);

	return (0);
}

FILE *
open_wmemstream(wchar_t **pbuf, size_t *psize)
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

	st->size = BUFSIZ * sizeof(wchar_t);
	if ((st->string = calloc(1, st->size)) == NULL) {
		free(st);
		fp->_flags = 0;
		return (NULL);
	}

	st->pos = 0;
	st->len = 0;
	st->pbuf = pbuf;
	st->psize = psize;
	bzero(&st->mbs, sizeof(st->mbs));

	*pbuf = st->string;
	*psize = st->len;

	fp->_flags = __SWR;
	fp->_file = -1;
	fp->_cookie = st;
	fp->_read = NULL;
	fp->_write = wmemstream_write;
	fp->_seek = wmemstream_seek;
	fp->_close = wmemstream_close;
	_SET_ORIENTATION(fp, 1);

	return (fp);
}
