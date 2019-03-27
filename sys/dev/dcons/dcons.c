/*-
 * SPDX-License-Identifier: BSD-4-Clause
 *
 * Copyright (C) 2003,2004
 * 	Hidetoshi Shimokawa. All rights reserved.
 * 
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *
 *	This product includes software developed by Hidetoshi Shimokawa.
 *
 * 4. Neither the name of the author nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 * 
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 * 
 * $Id: dcons.c,v 1.65 2003/10/24 03:24:55 simokawa Exp $
 * $FreeBSD$
 */

#include <sys/param.h>

#if defined(_BOOT)
#include "dcons.h"
#include "stand.h"
#else
#include <dev/dcons/dcons.h>
#endif

int
dcons_ischar(struct dcons_softc *dc)
{
	u_int32_t ptr, pos, gen, next_gen;
	struct dcons_ch *ch;

	ch = &dc->i;

	ptr = ntohl(*ch->ptr);
	gen = ptr >> DCONS_GEN_SHIFT;
	pos = ptr & DCONS_POS_MASK;
	if (gen == ch->gen && pos == ch->pos)
		return (0);

	next_gen = DCONS_NEXT_GEN(ch->gen);
	/* XXX sanity check */
	if ((gen != ch->gen && gen != next_gen)
			|| (gen == ch->gen && pos < ch->pos)) {
		/* generation skipped !! */
		/* XXX discard */
		ch->gen = gen;
		ch->pos = pos;
		return (0);
	}

	return (1);
}

int
dcons_checkc(struct dcons_softc *dc)
{
	unsigned char c;
	u_int32_t ptr, pos, gen, next_gen;
	struct dcons_ch *ch;

	ch = &dc->i;

	ptr = ntohl(*ch->ptr);
	gen = ptr >> DCONS_GEN_SHIFT;
	pos = ptr & DCONS_POS_MASK;
	if (gen == ch->gen && pos == ch->pos)
		return (-1);

	next_gen = DCONS_NEXT_GEN(ch->gen);
	/* XXX sanity check */
	if ((gen != ch->gen && gen != next_gen)
			|| (gen == ch->gen && pos < ch->pos)) {
		/* generation skipped !! */
		/* XXX discard */
		ch->gen = gen;
		ch->pos = pos;
		return (-1);
	}

	c = ch->buf[ch->pos];
	ch->pos ++;
	if (ch->pos >= ch->size) {
		ch->gen = next_gen;
		ch->pos = 0;
	}

	return (c);
}

void
dcons_putc(struct dcons_softc *dc, int c)
{
	struct dcons_ch *ch;

	ch = &dc->o;

	ch->buf[ch->pos] = c;
	ch->pos ++;
	if (ch->pos >= ch->size) {
		ch->gen = DCONS_NEXT_GEN(ch->gen);
		ch->pos = 0;
	}
	*ch->ptr = DCONS_MAKE_PTR(ch);
}

static int
dcons_init_port(int port, int offset, int size, struct dcons_buf *buf,
    struct dcons_softc *sc)
{
	int osize;
	struct dcons_softc *dc;

	dc = &sc[port];

	osize = size * 3 / 4;

	dc->o.size = osize;
	dc->i.size = size - osize;
	dc->o.buf = (char *)buf + offset;
	dc->i.buf = dc->o.buf + osize;
	dc->o.gen = dc->i.gen = 0;
	dc->o.pos = dc->i.pos = 0;
	dc->o.ptr = &buf->optr[port];
	dc->i.ptr = &buf->iptr[port];
	dc->brk_state = STATE0;
	buf->osize[port] = htonl(osize);
	buf->isize[port] = htonl(size - osize);
	buf->ooffset[port] = htonl(offset);
	buf->ioffset[port] = htonl(offset + osize);
	buf->optr[port] = DCONS_MAKE_PTR(&dc->o);
	buf->iptr[port] = DCONS_MAKE_PTR(&dc->i);

	return(0);
}

int
dcons_load_buffer(struct dcons_buf *buf, int size, struct dcons_softc *sc)
{
	int port, s;
	struct dcons_softc *dc;

	if (buf->version != htonl(DCONS_VERSION))
		return (-1);

	s = DCONS_HEADER_SIZE;
	for (port = 0; port < DCONS_NPORT; port ++) {
		dc = &sc[port];
		dc->o.size = ntohl(buf->osize[port]);
		dc->i.size = ntohl(buf->isize[port]);
		dc->o.buf = (char *)buf + ntohl(buf->ooffset[port]);
		dc->i.buf = (char *)buf + ntohl(buf->ioffset[port]);
		dc->o.gen = ntohl(buf->optr[port]) >> DCONS_GEN_SHIFT;
		dc->i.gen = ntohl(buf->iptr[port]) >> DCONS_GEN_SHIFT;
		dc->o.pos = ntohl(buf->optr[port]) & DCONS_POS_MASK;
		dc->i.pos = ntohl(buf->iptr[port]) & DCONS_POS_MASK;
		dc->o.ptr = &buf->optr[port];
		dc->i.ptr = &buf->iptr[port];
		dc->brk_state = STATE0;

		s += dc->o.size + dc->i.size;
	}

	/* sanity check */
	if (s > size)
		return (-1);

	buf->magic = ntohl(DCONS_MAGIC);

	return (0);
}

void
dcons_init(struct dcons_buf *buf, int size, struct dcons_softc *sc)
{
	int size0, size1, offset;

	offset = DCONS_HEADER_SIZE;
	size0 = (size - offset);
	size1 = size0 * 3 / 4;		/* console port buffer */

	dcons_init_port(0, offset, size1, buf, sc);
	offset += size1;
	dcons_init_port(1, offset, size0 - size1, buf, sc);
	buf->version = htonl(DCONS_VERSION);
	buf->magic = ntohl(DCONS_MAGIC);
}
