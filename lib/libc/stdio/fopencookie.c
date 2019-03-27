/*
 * Copyright (c) 2016, EMC / Isilon Storage Division
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS ``AS
 * IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO,
 * THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE COPYRIGHT HOLDERS OR
 * CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS;
 * OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
 * OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF
 * ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/fcntl.h>

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>

#include "local.h"

struct fopencookie_thunk {
	void			*foc_cookie;
	cookie_io_functions_t	foc_io;
};

static int _fopencookie_read(void *, char *, int);
static int _fopencookie_write(void *, const char *, int);
static fpos_t _fopencookie_seek(void *, fpos_t, int);
static int _fopencookie_close(void *);

FILE *
fopencookie(void *cookie, const char *mode, cookie_io_functions_t io_funcs)
{
	int (*readfn)(void *, char *, int);
	int (*writefn)(void *, const char *, int);
	struct fopencookie_thunk *thunk;
	FILE *fp;
	int flags, oflags;

	if ((flags = __sflags(mode, &oflags)) == 0)
		return (NULL);

	thunk = malloc(sizeof(*thunk));
	if (thunk == NULL)
		return (NULL);

	thunk->foc_cookie = cookie;
	thunk->foc_io = io_funcs;

	readfn = _fopencookie_read;
	writefn = _fopencookie_write;
	if (flags == __SWR)
		readfn = NULL;
	else if (flags == __SRD)
		writefn = NULL;

	fp = funopen(thunk, readfn, writefn, _fopencookie_seek,
	    _fopencookie_close);
	if (fp == NULL) {
		free(thunk);
		return (NULL);
	}

	if ((oflags & O_APPEND) != 0)
		fp->_flags |= __SAPP;

	return (fp);
}

static int
_fopencookie_read(void *cookie, char *buf, int size)
{
	struct fopencookie_thunk *thunk;

	thunk = cookie;

	/* Reads from a stream with NULL read return EOF. */
	if (thunk->foc_io.read == NULL)
		return (0);

	return ((int)thunk->foc_io.read(thunk->foc_cookie, buf, (size_t)size));
}

static int
_fopencookie_write(void *cookie, const char *buf, int size)
{
	struct fopencookie_thunk *thunk;

	thunk = cookie;

	/* Writes to a stream with NULL write discard data. */
	if (thunk->foc_io.write == NULL)
		return (size);

	return ((int)thunk->foc_io.write(thunk->foc_cookie, buf,
		(size_t)size));
}

static fpos_t
_fopencookie_seek(void *cookie, fpos_t offset, int whence)
{
	struct fopencookie_thunk *thunk;
	off64_t off64;
	int res;

	switch (whence) {
	case SEEK_SET:
	case SEEK_CUR:
	case SEEK_END:
		break;
	default:
		/* fopencookie(3) only allows these three seek modes. */
		errno = EINVAL;
		return (-1);
	}

	thunk = cookie;

	/*
	 * If seek is NULL, it is not possible to perform seek operations on
	 * the stream.
	 */
	if (thunk->foc_io.seek == NULL) {
		errno = ENOTSUP;
		return (-1);
	}

	off64 = (off64_t)offset;
	res = thunk->foc_io.seek(thunk->foc_cookie, &off64, whence);
	if (res < 0)
		return (res);

	return ((fpos_t)off64);
}

static int
_fopencookie_close(void *cookie)
{
	struct fopencookie_thunk *thunk;
	int ret, serrno;

	ret = 0;
	thunk = cookie;
	if (thunk->foc_io.close != NULL)
		ret = thunk->foc_io.close(thunk->foc_cookie);

	serrno = errno;
	free(thunk);
	errno = serrno;
	return (ret);
}
