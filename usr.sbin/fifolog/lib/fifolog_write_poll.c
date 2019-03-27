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
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <stdint.h>
#include <time.h>
#include <sys/endian.h>

#include <zlib.h>

#include "fifolog.h"
#include "libfifolog_int.h"
#include "fifolog_write.h"
#include "miniobj.h"

static int fifolog_write_gzip(struct fifolog_writer *f, time_t now);

#define ALLOC(ptr, size) do {                   \
	(*(ptr)) = calloc(1, size);             \
	assert(*(ptr) != NULL);                 \
} while (0)


const char *fifolog_write_statnames[] = {
	[FIFOLOG_PT_BYTES_PRE] =	"Bytes before compression",
	[FIFOLOG_PT_BYTES_POST] =	"Bytes after compression",
	[FIFOLOG_PT_WRITES] =		"Writes",
	[FIFOLOG_PT_FLUSH] =		"Flushes",
	[FIFOLOG_PT_SYNC] =		"Syncs",
	[FIFOLOG_PT_RUNTIME] =		"Runtime"
};

/**********************************************************************
 * Check that everything is all right
 */
static void
fifolog_write_assert(const struct fifolog_writer *f)
{

	CHECK_OBJ_NOTNULL(f, FIFOLOG_WRITER_MAGIC);
	assert(f->ff->zs->next_out + f->ff->zs->avail_out == \
	    f->obuf + f->obufsize);
}

/**********************************************************************
 * Allocate/Destroy a new fifolog writer instance
 */

struct fifolog_writer *
fifolog_write_new(void)
{
	struct fifolog_writer *f;

	ALLOC_OBJ(f, FIFOLOG_WRITER_MAGIC);
	assert(f != NULL);
	return (f);
}

void
fifolog_write_destroy(struct fifolog_writer *f)
{

	free(f->obuf);
	free(f->ibuf);
	FREE_OBJ(f);
}

/**********************************************************************
 * Open/Close the fifolog
 */

void
fifolog_write_close(struct fifolog_writer *f)
{
	time_t now;

	CHECK_OBJ_NOTNULL(f, FIFOLOG_WRITER_MAGIC);
	fifolog_write_assert(f);

	f->cleanup = 1;
	time(&now);
	fifolog_write_gzip(f, now);
	fifolog_write_assert(f);
	fifolog_int_close(&f->ff);
	free(f->ff);
}

const char *
fifolog_write_open(struct fifolog_writer *f, const char *fn,
    unsigned writerate, unsigned syncrate, unsigned compression)
{
	const char *es;
	int i;
	time_t now;
	off_t o;

	CHECK_OBJ_NOTNULL(f, FIFOLOG_WRITER_MAGIC);

	/* Check for legal compression value */
	if (compression > Z_BEST_COMPRESSION)
		return ("Illegal compression value");

	f->writerate = writerate;
	f->syncrate = syncrate;
	f->compression = compression;

	/* Reset statistics */
	memset(f->cnt, 0, sizeof f->cnt);

	es = fifolog_int_open(&f->ff, fn, 1);
	if (es != NULL)
		return (es);
	es = fifolog_int_findend(f->ff, &o);
	if (es != NULL)
		return (es);
	i = fifolog_int_read(f->ff, o);
	if (i)
		return ("Read error, looking for seq");
	f->seq = be32dec(f->ff->recbuf);
	if (f->seq == 0) {
		/* Empty fifolog */
		f->seq = random();
	} else {
		f->recno = o + 1;
		f->seq++;
	}

	f->obufsize = f->ff->recsize;
	ALLOC(&f->obuf, f->obufsize);

	f->ibufsize = f->obufsize * 10;
	ALLOC(&f->ibuf, f->ibufsize);
	f->ibufptr = 0;

	i = deflateInit(f->ff->zs, (int)f->compression);
	assert(i == Z_OK);

	f->flag |= FIFOLOG_FLG_RESTART;
	f->flag |= FIFOLOG_FLG_SYNC;
	f->ff->zs->next_out = f->obuf + 9;
	f->ff->zs->avail_out = f->obufsize - 9;

	time(&now);
	f->starttime = now;
	f->lastsync = now;
	f->lastwrite = now;

	fifolog_write_assert(f);
	return (NULL);
}

/**********************************************************************
 * Write an output record
 * Returns -1 if there are trouble writing data
 */

static int
fifolog_write_output(struct fifolog_writer *f, int fl, time_t now)
{
	long h, l = f->ff->zs->next_out - f->obuf;
	ssize_t i, w;
	int retval = 0;

	h = 4;					/* seq */
	be32enc(f->obuf, f->seq);
	f->obuf[h] = f->flag;
	h += 1;					/* flag */
	if (f->flag & FIFOLOG_FLG_SYNC) {
		be32enc(f->obuf + h, now);
		h += 4;				/* timestamp */
	}

	assert(l <= (long)f->ff->recsize);	/* NB: l includes h */
	assert(l >= h);

	/* We will never write an entirely empty buffer */
	if (l == h)
		return (0);

	if (l < (long)f->ff->recsize && fl == Z_NO_FLUSH)
		return (0);

	w = f->ff->recsize - l;
	if (w >  255) {
		be32enc(f->obuf + f->ff->recsize - 4, w);
		f->obuf[4] |= FIFOLOG_FLG_4BYTE;
	} else if (w > 0) {
		f->obuf[f->ff->recsize - 1] = (uint8_t)w;
		f->obuf[4] |= FIFOLOG_FLG_1BYTE;
	}

	f->cnt[FIFOLOG_PT_BYTES_POST] += l - h;

	i = pwrite(f->ff->fd, f->obuf, f->ff->recsize,
	    (f->recno + 1) * f->ff->recsize);
	if (i != f->ff->recsize)
		retval = -1;
	else
		retval = 1;

	f->cnt[FIFOLOG_PT_WRITES]++;
	f->cnt[FIFOLOG_PT_RUNTIME] = now - f->starttime;

	f->lastwrite = now;
	/*
	 * We increment these even on error, so as to properly skip bad,
	 * sectors or other light trouble.
	 */
	f->seq++;
	f->recno++;
	f->flag = 0;

	memset(f->obuf, 0, f->obufsize);
	f->ff->zs->next_out = f->obuf + 5;
	f->ff->zs->avail_out = f->obufsize - 5;
	return (retval);
}

/**********************************************************************
 * Run the compression engine
 * Returns -1 if there are trouble writing data
 */

static int
fifolog_write_gzip(struct fifolog_writer *f, time_t now)
{
	int i, fl, retval = 0;

	assert(now != 0);
	if (f->cleanup || now >= (int)(f->lastsync + f->syncrate)) {
		f->cleanup = 0;
		fl = Z_FINISH;
		f->cnt[FIFOLOG_PT_SYNC]++;
	} else if (now >= (int)(f->lastwrite + f->writerate)) {
		fl = Z_SYNC_FLUSH;
		f->cnt[FIFOLOG_PT_FLUSH]++;
	} else if (f->ibufptr == 0)
		return (0);
	else
		fl = Z_NO_FLUSH;

	f->ff->zs->avail_in = f->ibufptr;
	f->ff->zs->next_in = f->ibuf;

	while (1) {
		i = deflate(f->ff->zs, fl);
		assert(i == Z_OK || i == Z_BUF_ERROR || i == Z_STREAM_END);

		i = fifolog_write_output(f, fl, now);
		if (i == 0)
			break;
		if (i < 0)
			retval = -1;
	}
	assert(f->ff->zs->avail_in == 0);
	f->ibufptr = 0;
	if (fl == Z_FINISH) {
		f->flag |= FIFOLOG_FLG_SYNC;
		f->ff->zs->next_out = f->obuf + 9;
		f->ff->zs->avail_out = f->obufsize - 9;
		f->lastsync = now;
		assert(Z_OK == deflateReset(f->ff->zs));
	}
	return (retval);
}

/**********************************************************************
 * Poll to see if we need to flush out a record
 * Returns -1 if there are trouble writing data
 */

int
fifolog_write_poll(struct fifolog_writer *f, time_t now)
{

	if (now == 0)
		time(&now);
	return (fifolog_write_gzip(f, now));
}

/**********************************************************************
 * Attempt to write an entry into the ibuf.
 * Return zero if there is no space, one otherwise
 */

int
fifolog_write_record(struct fifolog_writer *f, uint32_t id, time_t now,
    const void *ptr, ssize_t len)
{
	const unsigned char *p;
	uint8_t buf[9];
	ssize_t bufl;

	fifolog_write_assert(f);
	assert(!(id & (FIFOLOG_TIMESTAMP|FIFOLOG_LENGTH)));
	assert(ptr != NULL);

	p = ptr;
	if (len == 0) {
		len = strlen(ptr);
		len++;
	} else {
		assert(len <= 255);
		id |= FIFOLOG_LENGTH;
	}
	assert (len > 0);

	/* Do a timestamp, if needed */
	if (now == 0)
		time(&now);

	if (now != f->last)
		id |= FIFOLOG_TIMESTAMP;

	/* Emit instance+flag */
	be32enc(buf, id);
	bufl = 4;

	if (id & FIFOLOG_TIMESTAMP) {
		be32enc(buf + bufl, (uint32_t)now);
		bufl += 4;
	}
	if (id & FIFOLOG_LENGTH)
		buf[bufl++] = (u_char)len;

	if (bufl + len + f->ibufptr > f->ibufsize)
		return (0);

	memcpy(f->ibuf + f->ibufptr, buf, bufl);
	f->ibufptr += bufl;
	memcpy(f->ibuf + f->ibufptr, p, len);
	f->ibufptr += len;
	f->cnt[FIFOLOG_PT_BYTES_PRE] += bufl + len;

	if (id & FIFOLOG_TIMESTAMP)
		f->last = now;
	return (1);
}

/**********************************************************************
 * Write an entry, polling the gzip/writer until success.
 * Long binary entries are broken into 255 byte chunks.
 * Returns -1 if there are problems writing data
 */

int
fifolog_write_record_poll(struct fifolog_writer *f, uint32_t id, time_t now,
    const void *ptr, ssize_t len)
{
	u_int l;
	const unsigned char *p;
	int retval = 0;

	if (now == 0)
		time(&now);
	fifolog_write_assert(f);

	assert(!(id & (FIFOLOG_TIMESTAMP|FIFOLOG_LENGTH)));
	assert(ptr != NULL);

	if (len == 0) {
		if (!fifolog_write_record(f, id, now, ptr, len)) {
			if (fifolog_write_gzip(f, now) < 0)
				retval = -1;
			/* The string could be too long for the ibuf, so... */
			if (!fifolog_write_record(f, id, now, ptr, len))
				retval = -1;
		}
	} else {
		for (p = ptr; len > 0; len -= l, p += l) {
			l = len;
			if (l > 255)
				l = 255;
			while (!fifolog_write_record(f, id, now, p, l))
				if (fifolog_write_gzip(f, now) < 0)
					retval = -1;
		}
	}
	if (fifolog_write_gzip(f, now) < 0)
		retval = -1;
	fifolog_write_assert(f);
	return (retval);
}
