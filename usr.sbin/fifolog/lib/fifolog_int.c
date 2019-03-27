/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2005-2008 Poul-Henning Kamp
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
 * $FreeBSD$
 */

#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <zlib.h>

#include <sys/disk.h>
#include <sys/endian.h>
#include <sys/stat.h>

#include "miniobj.h"
#include "fifolog.h"
#include "libfifolog_int.h"

/*
 * Open a fifolog file or partition for reading or writing.
 *
 * Return value is NULL for success or a error description string to
 * be augmented by errno if non-zero.
 *
 * The second function is just an error-handling wrapper around the
 * first which, does the actual work.
 */

static const char *
fifolog_int_open_i(struct fifolog_file *f, const char *fname, int mode)
{
	struct stat st;
	ssize_t u;
	int i;

	f->fd = open(fname, mode ? O_RDWR : O_RDONLY);
	if (f->fd < 0)
		return ("Cannot open");

	/* Determine initial record size guesstimate */
	i = ioctl(f->fd, DIOCGSECTORSIZE, &f->recsize);
	if (i != 0 && errno != ENOTTY)
		return ("ioctl(DIOCGSECTORSIZE) failed");

	if (i != 0) {
		i = fstat(f->fd, &st);
		assert(i == 0);
		if (!S_ISREG(st.st_mode))
			return ("Neither disk nor regular file");
		f->recsize = 512;
		f->logsize = st.st_size;
	} else if (f->recsize < 64) {
		return ("Disk device sectorsize smaller than 64");
	} else {
		i = ioctl(f->fd, DIOCGMEDIASIZE, &f->logsize);
		if (i < 0 && errno != ENOTTY)
			return ("ioctl(DIOCGMEDIASIZE) failed");
	}

	/* Allocate a record buffer */
	f->recbuf = malloc(f->recsize);
	if (f->recbuf == NULL)
		return ("Cannot malloc");

	/* Read and validate the label sector */
	i = pread(f->fd, f->recbuf, f->recsize, 0);
	if (i < 0 || i < (int)f->recsize)
		return ("Read error, first sector");

	errno = 0;
	if (memcmp(f->recbuf, FIFOLOG_FMT_MAGIC, strlen(FIFOLOG_FMT_MAGIC) + 1))
		return ("Wrong or missing magic string");

	u = be32dec(f->recbuf + FIFOLOG_OFF_BS);
	if (u < 64)
		return ("Wrong record size in header (<64)");

	if ((off_t)u >= f->logsize)
		return ("Record size in header bigger than fifolog");

	f->recsize = u;

	/* Reallocate the buffer to correct size if necessary */
	if (u != f->recsize) {
		free(f->recbuf);
		f->recbuf = NULL;
		f->recsize = u;
		f->recbuf = malloc(f->recsize);
		if (f->recbuf == NULL)
			return ("Cannot malloc");
	}

	/* Calculate number of records in fifolog */
	f->logsize /= u;
	if (f->logsize < 10)
		return ("less than 10 records in fifolog");

	f->logsize--;		/* the label record */

	/* Initialize zlib handling */

	f->zs = calloc(1, sizeof(*f->zs));
	if (f->zs == NULL)
		return ("cannot malloc");

	return (NULL);
}

const char *
fifolog_int_open(struct fifolog_file **ff, const char *fname, int mode)
{
	struct fifolog_file fs, *f;
	const char *retval;
	int e;

	f = &fs;
	memset(f, 0, sizeof *f);
	f->fd = -1;
	retval = fifolog_int_open_i(f, fname, mode);
	e = errno;
	if (retval == NULL) {
		*ff = malloc(sizeof *f);
		if (*ff != NULL) {
			memcpy(*ff, f, sizeof *f);
			(*ff)->magic = FIFOLOG_FILE_MAGIC;
			return (retval);
		}
	}
	fifolog_int_close(&f);
	errno = e;
	return (retval);
}

void
fifolog_int_close(struct fifolog_file **ff)
{
	struct fifolog_file *f;

	f = *ff;
	*ff = NULL;
	if (f == NULL)
		return;

	if (f->fd >= 0)
		(void)close(f->fd);
	if (f->zs != NULL)
		free(f->zs);
	if (f->recbuf != NULL)
		free(f->recbuf);
}

static void
fifolog_int_file_assert(const struct fifolog_file *ff)
{

	CHECK_OBJ_NOTNULL(ff, FIFOLOG_FILE_MAGIC);
	assert(ff->fd >= 0);
	assert(ff->recbuf != NULL);
}


/*
 * Read a record.
 *
 * Return zero on success
 */

int
fifolog_int_read(const struct fifolog_file *ff, off_t recno)
{
	int i;

	fifolog_int_file_assert(ff);
	if (recno >= ff->logsize)
		return (-1);
	recno++;			/* label sector */
	i = pread(ff->fd, ff->recbuf, ff->recsize, recno * ff->recsize);
	if (i < 0)
		return (-2);
	if (i != (int)ff->recsize)
		return (-3);
	return (0);
}

/*
 * Find the last written record in the fifolog.
 *
 * Return is error string or NULL on success
 */

const char *
fifolog_int_findend(const struct fifolog_file *ff, off_t *last)
{
	off_t o, s;
	int e;
	unsigned seq0, seq;

	fifolog_int_file_assert(ff);

	o = 0;
	e = fifolog_int_read(ff, o);
	if (e)
		return("Read error, first record");

	seq0 = be32dec(ff->recbuf);

	/* If the first records sequence is zero, the fifolog is empty */
	if (seq0 == 0) {
		*last = o;
		return (NULL);
	}

	/* Do a binary search for a discontinuity in the sequence numbers */
	s = ff->logsize / 2;
	do {
		e = fifolog_int_read(ff, o + s);
		if (e)
			return ("Read error while searching");
		seq = be32dec(ff->recbuf);
		if (seq == seq0 + s) {
			o += s;
			seq0 = seq;
		}
		s /= 2;
		assert(o < ff->logsize);
	} while (s > 0);

	*last = o;
	return (NULL);
}
