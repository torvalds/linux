/*-
 * Copyright (c) 2014 Pedro Souza <pedrosouza@freebsd.org>
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
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include "lstd.h"
#include "math.h"

#ifdef LOADER_VERIEXEC
#include <verify_file.h>
#endif

FILE *
fopen(const char *filename, const char *mode)
{
	struct stat	st;
	int		fd, m, o;
	FILE		*f;

	if (mode == NULL)
		return NULL;

	switch (*mode++) {
	case 'r':	/* open for reading */
		m = O_RDONLY;
		o = 0;
		break;

	case 'w':	/* open for writing */
		m = O_WRONLY;
		/* These are not actually implemented yet */
		o = O_CREAT | O_TRUNC;
		break;

	default:	/* illegal mode */
		return (NULL);
	}

	if (*mode == '+')
		m = O_RDWR;

	fd = open(filename, m | o);
	if (fd < 0)
		return NULL;

	f = malloc(sizeof(FILE));
	if (f == NULL) {
		close(fd);
		return NULL;
	}

	if (fstat(fd, &st) != 0) {
		free(f);
		close(fd);
		return (NULL);
	}

#ifdef LOADER_VERIEXEC
	/* only regular files and only reading makes sense */
	if (S_ISREG(st.st_mode) && !(m & O_WRONLY)) {
		if (verify_file(fd, filename, 0, VE_GUESS) < 0) {
			free(f);
			close(fd);
			return (NULL);
		}
	}
#endif

	f->fd = fd;
	f->offset = 0;
	f->size = st.st_size;

	return (f);
}


FILE *
freopen(const char *filename, const char *mode, FILE *stream)
{
	fclose(stream);
	return (fopen(filename, mode));
}

size_t
fread(void *ptr, size_t size, size_t count, FILE *stream)
{
	size_t r;

	if (stream == NULL)
		return 0;
	r = (size_t)read(stream->fd, ptr, size * count);
	stream->offset += r;

	return (r);
}

size_t
fwrite(const void *ptr, size_t size, size_t count, FILE *stream)
{
	ssize_t w;

	if (stream == NULL || ptr == NULL)
		return (0);
	w = write(stream->fd, ptr, size * count);
	if (w == -1)
		return (0);

	stream->offset += w;
	return ((size_t)w);
}

int
fclose(FILE *stream)
{
	if (stream == NULL)
		return EOF;
	close(stream->fd);
	free(stream);

	return (0);
}

int
ferror(FILE *stream)
{

	return (stream == NULL || stream->fd < 0);
}

int
feof(FILE *stream)
{

	if (stream == NULL)
		return 1;

	return (stream->offset >= stream->size);
}

int
getc(FILE *stream)
{
	char	ch;
	size_t	r;

	if (stream == NULL)
		return EOF;
	r = read(stream->fd, &ch, 1);
	if (r == 1)
		return ch;
	return EOF;
}

DIR *
opendir(const char *name)
{
	DIR *dp;
	int fd;

	fd = open(name, O_RDONLY);
	if (fd < 0)
		return NULL;
	dp = fdopendir(fd);
	if (dp == NULL)
		close(fd);
	return dp;
}

DIR *
fdopendir(int fd)
{
	DIR *dp;

	dp = malloc(sizeof(*dp));
	if (dp == NULL)
		return NULL;
	dp->fd = fd;
	return dp;
}

int
closedir(DIR *dp)
{
	close(dp->fd);
	dp->fd = -1;
	free(dp);
	return 0;
}

void
luai_writestring(const char *s, int i)
{

	while (i-- > 0)
		putchar(*s++);
}

/*
 * These routines from here on down are to implement the lua math
 * library, but that's not presently included by default. They are
 * little more than placeholders to allow compilation due to linkage
 * issues with upstream Lua.
 */

int64_t
lstd_pow(int64_t x, int64_t y)
{
	int64_t rv = 1;

	if (y < 0)
		return 0;
	rv = x;
	while (--y)
		rv *= x;

	return rv;
}

int64_t
lstd_floor(int64_t x)
{

	return (x);
}

int64_t
lstd_fmod(int64_t a, int64_t b)
{

	return (a % b);
}

/*
 * This can't be implemented, so maybe it should just abort.
 */
int64_t
lstd_frexp(int64_t a, int *y)
{
	*y = 0;

	return 0;
}
