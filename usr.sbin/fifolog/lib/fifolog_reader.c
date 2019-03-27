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

#include <stdio.h>
#include <unistd.h>
#include <assert.h>
#include <err.h>
#include <time.h>
#include <string.h>
#include <stdlib.h>
#include <zlib.h>
#include <sys/endian.h>

#include "fifolog.h"
#include "libfifolog.h"
#include "libfifolog_int.h"
#include "miniobj.h"

/*--------------------------------------------------------------------*/

struct fifolog_reader {
	unsigned		magic;
#define FIFOLOG_READER_MAGIC	0x1036d139
	struct fifolog_file	*ff;
	unsigned		olen;
	unsigned char		*obuf;
	time_t			now;
};

struct fifolog_reader *
fifolog_reader_open(const char *fname)
{
	const char *retval;
	struct fifolog_reader *fr;
	int i;

	fr = calloc(1, sizeof(*fr));
	if (fr == NULL)
		err(1, "Cannot malloc");

	retval = fifolog_int_open(&fr->ff, fname, 0);
	if (retval != NULL)
		err(1, "%s", retval);

	fr->obuf = calloc(16, fr->ff->recsize);
	if (fr->obuf == NULL)
		err(1, "Cannot malloc");
	fr->olen = fr->ff->recsize * 16;

	i = inflateInit(fr->ff->zs);
	assert(i == Z_OK);

	fr->magic = FIFOLOG_READER_MAGIC;
	return (fr);
}

/*
 * Find the next SYNC block
 *
 * Return:
 *	0 - empty fifolog
 *	1 - found sync block
 *	2 - would have wrapped around
 *	3 - End of written log.
 */

static int
fifolog_reader_findsync(const struct fifolog_file *ff, off_t *o)
{
	int e;
	unsigned seq, seqs;

	assert(*o < ff->logsize);
	e = fifolog_int_read(ff, *o);
	if (e)
		err(1, "Read error (%d) while looking for SYNC", e);
	seq = be32dec(ff->recbuf);
	if (*o == 0 && seq == 0)
		return (0);

	if (ff->recbuf[4] & FIFOLOG_FLG_SYNC)
		return (1);		/* That was easy... */
	while(1) {
		assert(*o < ff->logsize);
		(*o)++;
		seq++;
		if (*o == ff->logsize)
			return (2);	/* wraparound */
		e = fifolog_int_read(ff, *o);
		if (e)
			err(1, "Read error (%d) while looking for SYNC", e);
		seqs = be32dec(ff->recbuf);
		if (seqs != seq)
			return (3);		/* End of log */
		if (ff->recbuf[4] & FIFOLOG_FLG_SYNC)
			return (1);		/* Bingo! */
	}
}

/*
 * Seek out a given timestamp
 */

off_t
fifolog_reader_seek(const struct fifolog_reader *fr, time_t t0)
{
	off_t o, s, st;
	time_t t, tt;
	unsigned seq, seqs;
	const char *retval;
	int e;

	CHECK_OBJ_NOTNULL(fr, FIFOLOG_READER_MAGIC);

	/*
	 * First, find the first SYNC block
	 */
	o = 0;
	e = fifolog_reader_findsync(fr->ff, &o);
	if (e == 0)
		return (0);			/* empty fifolog */
	assert(e == 1);

	assert(fr->ff->recbuf[4] & FIFOLOG_FLG_SYNC);
	seq = be32dec(fr->ff->recbuf);
	t = be32dec(fr->ff->recbuf + 5);

	if (t > t0) {
		/* Check if there is a second older part we can use */
		retval = fifolog_int_findend(fr->ff, &s);
		if (retval != NULL)
			err(1, "%s", retval);
		s++;
		e = fifolog_reader_findsync(fr->ff, &s);
		if (e == 0)
			return (0);		/* empty fifolog */
		if (e == 1) {
			o = s;
			seq = be32dec(fr->ff->recbuf);
			t = be32dec(fr->ff->recbuf + 5);
		}
	}

	/* Now do a binary search to find the sync block right before t0 */
	s = st = (fr->ff->logsize - o) / 2;
	while (s > 1) {
		/* We know we shouldn't wrap */
		if (o + st > fr->ff->logsize + 1) {
			s = st = s / 2;
			continue;
		}
		e = fifolog_int_read(fr->ff, o + st);
		if (e) {
			s = st = s / 2;
			continue;
		}
		/* If not in same part, sequence won't match */
		seqs = be32dec(fr->ff->recbuf);
		if (seqs != seq + st) {
			s = st = s / 2;
			continue;
		}
		/* If not sync block, try next */
		if (!(fr->ff->recbuf[4] & FIFOLOG_FLG_SYNC)) {
			st++;
			continue;
		}
		/* Check timestamp */
		tt = be32dec(fr->ff->recbuf + 5);
		if (tt >= t0) {
			s = st = s / 2;
			continue;
		}
		o += st;
		seq = seqs;
	}
	fprintf(stderr, "Read from %jx\n", o * fr->ff->recsize);
	return (o);
}

static unsigned char *
fifolog_reader_chop(struct fifolog_reader *fr, fifolog_reader_render_t *func, void *priv)
{
	u_char *p, *q;
	uint32_t v, w, u;

	p = fr->obuf;
	q = fr->obuf + (fr->olen - fr->ff->zs->avail_out);

	while (1) {
		/* Make sure we have a complete header */
		if (p + 5 >= q)
			return (p);
		w = 4;
		u = be32dec(p);
		if (u & FIFOLOG_TIMESTAMP) {
			fr->now = be32dec(p + 4);
			w += 4;
		}
		if (u & FIFOLOG_LENGTH) {
			v = p[w];
			w++;
			if (p + w + v >= q)
				return (p);
		} else {
			for (v = 0; p + v + w < q && p[v + w] != '\0'; v++)
				continue;
			if (p + v + w >= q)
				return (p);
			v++;
		}
		func(priv, fr->now, u, p + w, v);
		p += w + v;
	}
}

/*
 * Process fifolog until end of written log or provided timestamp
 */

void
fifolog_reader_process(struct fifolog_reader *fr, off_t from, fifolog_reader_render_t *func, void *priv, time_t end)
{
	uint32_t seq, lseq;
	off_t o = from;
	int i, e;
	time_t t;
	u_char *p, *q;
	z_stream *zs;

	CHECK_OBJ_NOTNULL(fr, FIFOLOG_READER_MAGIC);
	zs = fr->ff->zs;
	lseq = 0;
	while (1) {
		e = fifolog_int_read(fr->ff, o);
		if (e)
			err(1, "Read error (%d)", e);
		if (++o >= fr->ff->logsize)
			o = 0;
		seq = be32dec(fr->ff->recbuf);
		if (lseq != 0 && seq != lseq + 1)
			break;
		lseq = seq;
		zs->avail_in = fr->ff->recsize - 5;
		zs->next_in = fr->ff->recbuf + 5;
		if (fr->ff->recbuf[4] & FIFOLOG_FLG_1BYTE)
			zs->avail_in -= fr->ff->recbuf[fr->ff->recsize - 1];
		if (fr->ff->recbuf[4] & FIFOLOG_FLG_4BYTE)
			zs->avail_in -=
			    be32dec(fr->ff->recbuf + fr->ff->recsize - 4);
		if (fr->ff->recbuf[4] & FIFOLOG_FLG_SYNC) {
			i = inflateReset(zs);
			assert(i == Z_OK);
			zs->next_out = fr->obuf;
			zs->avail_out = fr->olen;
			t = be32dec(fr->ff->recbuf + 5);
			if (t > end)
				break;
			zs->next_in += 4;
			zs->avail_in -= 4;
		}

		while(zs->avail_in > 0) {
			i = inflate(zs, 0);
			if (i == Z_BUF_ERROR) {
#if 1
				fprintf(stderr,
				    "Z_BUF_ERROR [%d,%d] [%d,%d,%d]\n",
				    (int)(zs->next_in - fr->ff->recbuf),
				    zs->avail_in,
				    (int)(zs->next_out - fr->obuf),
				    zs->avail_out, fr->olen);
				exit (250);
#else

				i = Z_OK;
#endif
			}
			if (i == Z_STREAM_END) {
				i = inflateReset(zs);
			}
			if (i != Z_OK) {
				fprintf(stderr, "inflate = %d\n", i);
				exit (250);
			}
			assert(i == Z_OK);
			if (zs->avail_out != fr->olen) {
				q = fr->obuf + (fr->olen - zs->avail_out);
				p = fifolog_reader_chop(fr, func, priv);
				if (p < q)
					(void)memmove(fr->obuf, p, q - p);
				zs->avail_out = fr->olen - (q - p);
				zs->next_out = fr->obuf + (q - p);
			}
		}
	}
}
