/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2008 Ed Schouten <ed@FreeBSD.org>
 * All rights reserved.
 *
 * Portions of this software were developed under sponsorship from Snow
 * B.V., the Netherlands.
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
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/kernel.h>
#include <sys/lock.h>
#include <sys/queue.h>
#include <sys/sysctl.h>
#include <sys/systm.h>
#include <sys/tty.h>
#include <sys/uio.h>

#include <vm/uma.h>

/*
 * TTY input queue buffering.
 *
 * Unlike the output queue, the input queue has more features that are
 * needed to properly implement various features offered by the TTY
 * interface:
 *
 * - Data can be removed from the tail of the queue, which is used to
 *   implement backspace.
 * - Once in a while, input has to be `canonicalized'. When ICANON is
 *   turned on, this will be done after a CR has been inserted.
 *   Otherwise, it should be done after any character has been inserted.
 * - The input queue can store one bit per byte, called the quoting bit.
 *   This bit is used by TTYDISC to make backspace work on quoted
 *   characters.
 *
 * In most cases, there is probably less input than output, so unlike
 * the outq, we'll stick to 128 byte blocks here.
 */

static int ttyinq_flush_secure = 1;
SYSCTL_INT(_kern, OID_AUTO, tty_inq_flush_secure, CTLFLAG_RW,
	&ttyinq_flush_secure, 0, "Zero buffers while flushing");

#define TTYINQ_QUOTESIZE	(TTYINQ_DATASIZE / BMSIZE)
#define BMSIZE			32
#define GETBIT(tib,boff) \
	((tib)->tib_quotes[(boff) / BMSIZE] & (1 << ((boff) % BMSIZE)))
#define SETBIT(tib,boff) \
	((tib)->tib_quotes[(boff) / BMSIZE] |= (1 << ((boff) % BMSIZE)))
#define CLRBIT(tib,boff) \
	((tib)->tib_quotes[(boff) / BMSIZE] &= ~(1 << ((boff) % BMSIZE)))

struct ttyinq_block {
	struct ttyinq_block	*tib_prev;
	struct ttyinq_block	*tib_next;
	uint32_t		tib_quotes[TTYINQ_QUOTESIZE];
	char			tib_data[TTYINQ_DATASIZE];
};

static uma_zone_t ttyinq_zone;

#define	TTYINQ_INSERT_TAIL(ti, tib) do {				\
	if (ti->ti_end == 0) {						\
		tib->tib_prev = NULL;					\
		tib->tib_next = ti->ti_firstblock;			\
		ti->ti_firstblock = tib;				\
	} else {							\
		tib->tib_prev = ti->ti_lastblock;			\
		tib->tib_next = ti->ti_lastblock->tib_next;		\
		ti->ti_lastblock->tib_next = tib;			\
	}								\
	if (tib->tib_next != NULL)					\
		tib->tib_next->tib_prev = tib;				\
	ti->ti_nblocks++;						\
} while (0)

#define	TTYINQ_REMOVE_HEAD(ti) do {					\
	ti->ti_firstblock = ti->ti_firstblock->tib_next;		\
	if (ti->ti_firstblock != NULL)					\
		ti->ti_firstblock->tib_prev = NULL;			\
	ti->ti_nblocks--;						\
} while (0)

#define	TTYINQ_RECYCLE(ti, tib) do {					\
	if (ti->ti_quota <= ti->ti_nblocks)				\
		uma_zfree(ttyinq_zone, tib);				\
	else								\
		TTYINQ_INSERT_TAIL(ti, tib);				\
} while (0)

int 
ttyinq_setsize(struct ttyinq *ti, struct tty *tp, size_t size)
{
	struct ttyinq_block *tib;

	ti->ti_quota = howmany(size, TTYINQ_DATASIZE);

	while (ti->ti_quota > ti->ti_nblocks) {
		/*
		 * List is getting bigger.
		 * Add new blocks to the tail of the list.
		 *
		 * We must unlock the TTY temporarily, because we need
		 * to allocate memory. This won't be a problem, because
		 * in the worst case, another thread ends up here, which
		 * may cause us to allocate too many blocks, but this
		 * will be caught by the loop below.
		 */
		tty_unlock(tp);
		tib = uma_zalloc(ttyinq_zone, M_WAITOK);
		tty_lock(tp);

		if (tty_gone(tp)) {
			uma_zfree(ttyinq_zone, tib);
			return (ENXIO);
		}

		TTYINQ_INSERT_TAIL(ti, tib);
	}
	return (0);
}

void
ttyinq_free(struct ttyinq *ti)
{
	struct ttyinq_block *tib;

	ttyinq_flush(ti);
	ti->ti_quota = 0;

	while ((tib = ti->ti_firstblock) != NULL) {
		TTYINQ_REMOVE_HEAD(ti);
		uma_zfree(ttyinq_zone, tib);
	}

	MPASS(ti->ti_nblocks == 0);
}

int
ttyinq_read_uio(struct ttyinq *ti, struct tty *tp, struct uio *uio,
    size_t rlen, size_t flen)
{

	MPASS(rlen <= uio->uio_resid);

	while (rlen > 0) {
		int error;
		struct ttyinq_block *tib;
		size_t cbegin, cend, clen;

		/* See if there still is data. */
		if (ti->ti_begin == ti->ti_linestart)
			return (0);
		tib = ti->ti_firstblock;
		if (tib == NULL)
			return (0);

		/*
		 * The end address should be the lowest of these three:
		 * - The write pointer
		 * - The blocksize - we can't read beyond the block
		 * - The end address if we could perform the full read
		 */
		cbegin = ti->ti_begin;
		cend = MIN(MIN(ti->ti_linestart, ti->ti_begin + rlen),
		    TTYINQ_DATASIZE);
		clen = cend - cbegin;
		MPASS(clen >= flen);
		rlen -= clen;

		/*
		 * We can prevent buffering in some cases:
		 * - We need to read the block until the end.
		 * - We don't need to read the block until the end, but
		 *   there is no data beyond it, which allows us to move
		 *   the write pointer to a new block.
		 */
		if (cend == TTYINQ_DATASIZE || cend == ti->ti_end) {
			/*
			 * Fast path: zero copy. Remove the first block,
			 * so we can unlock the TTY temporarily.
			 */
			TTYINQ_REMOVE_HEAD(ti);
			ti->ti_begin = 0;

			/*
			 * Because we remove the first block, we must
			 * fix up the block offsets.
			 */
#define CORRECT_BLOCK(t) do {			\
	if (t <= TTYINQ_DATASIZE)		\
		t = 0;				\
	else					\
		t -= TTYINQ_DATASIZE;		\
} while (0)
			CORRECT_BLOCK(ti->ti_linestart);
			CORRECT_BLOCK(ti->ti_reprint);
			CORRECT_BLOCK(ti->ti_end);
#undef CORRECT_BLOCK

			/*
			 * Temporary unlock and copy the data to
			 * userspace. We may need to flush trailing
			 * bytes, like EOF characters.
			 */
			tty_unlock(tp);
			error = uiomove(tib->tib_data + cbegin,
			    clen - flen, uio);
			tty_lock(tp);

			/* Block can now be readded to the list. */
			TTYINQ_RECYCLE(ti, tib);
		} else {
			char ob[TTYINQ_DATASIZE - 1];

			/*
			 * Slow path: store data in a temporary buffer.
			 */
			memcpy(ob, tib->tib_data + cbegin, clen - flen);
			ti->ti_begin += clen;
			MPASS(ti->ti_begin < TTYINQ_DATASIZE);

			/* Temporary unlock and copy the data to userspace. */
			tty_unlock(tp);
			error = uiomove(ob, clen - flen, uio);
			tty_lock(tp);
		}

		if (error != 0)
			return (error);
		if (tty_gone(tp))
			return (ENXIO);
	}

	return (0);
}

static __inline void
ttyinq_set_quotes(struct ttyinq_block *tib, size_t offset,
    size_t length, int value)
{

	if (value) {
		/* Set the bits. */
		for (; length > 0; length--, offset++)
			SETBIT(tib, offset);
	} else {
		/* Unset the bits. */
		for (; length > 0; length--, offset++)
			CLRBIT(tib, offset);
	}
}

size_t
ttyinq_write(struct ttyinq *ti, const void *buf, size_t nbytes, int quote)
{
	const char *cbuf = buf;
	struct ttyinq_block *tib;
	unsigned int boff;
	size_t l;

	while (nbytes > 0) {
		boff = ti->ti_end % TTYINQ_DATASIZE;

		if (ti->ti_end == 0) {
			/* First time we're being used or drained. */
			MPASS(ti->ti_begin == 0);
			tib = ti->ti_firstblock;
			if (tib == NULL) {
				/* Queue has no blocks. */
				break;
			}
			ti->ti_lastblock = tib;
		} else if (boff == 0) {
			/* We reached the end of this block on last write. */
			tib = ti->ti_lastblock->tib_next;
			if (tib == NULL) {
				/* We've reached the watermark. */
				break;
			}
			ti->ti_lastblock = tib;
		} else {
			tib = ti->ti_lastblock;
		}

		/* Don't copy more than was requested. */
		l = MIN(nbytes, TTYINQ_DATASIZE - boff);
		MPASS(l > 0);
		memcpy(tib->tib_data + boff, cbuf, l);

		/* Set the quoting bits for the proper region. */
		ttyinq_set_quotes(tib, boff, l, quote);

		cbuf += l;
		nbytes -= l;
		ti->ti_end += l;
	}

	return (cbuf - (const char *)buf);
}

int
ttyinq_write_nofrag(struct ttyinq *ti, const void *buf, size_t nbytes, int quote)
{
	size_t ret __unused;

	if (ttyinq_bytesleft(ti) < nbytes)
		return (-1);

	/* We should always be able to write it back. */
	ret = ttyinq_write(ti, buf, nbytes, quote);
	MPASS(ret == nbytes);

	return (0);
}

void
ttyinq_canonicalize(struct ttyinq *ti)
{

	ti->ti_linestart = ti->ti_reprint = ti->ti_end;
	ti->ti_startblock = ti->ti_reprintblock = ti->ti_lastblock;
}

size_t
ttyinq_findchar(struct ttyinq *ti, const char *breakc, size_t maxlen,
    char *lastc)
{
	struct ttyinq_block *tib = ti->ti_firstblock;
	unsigned int boff = ti->ti_begin;
	unsigned int bend = MIN(MIN(TTYINQ_DATASIZE, ti->ti_linestart),
	    ti->ti_begin + maxlen);

	MPASS(maxlen > 0);

	if (tib == NULL)
		return (0);

	while (boff < bend) {
		if (strchr(breakc, tib->tib_data[boff]) && !GETBIT(tib, boff)) {
			*lastc = tib->tib_data[boff];
			return (boff - ti->ti_begin + 1);
		}
		boff++;
	}

	/* Not found - just process the entire block. */
	return (bend - ti->ti_begin);
}

void
ttyinq_flush(struct ttyinq *ti)
{
	struct ttyinq_block *tib;

	ti->ti_begin = 0;
	ti->ti_linestart = 0;
	ti->ti_reprint = 0;
	ti->ti_end = 0;

	/* Zero all data in the input queue to get rid of passwords. */
	if (ttyinq_flush_secure) {
		for (tib = ti->ti_firstblock; tib != NULL; tib = tib->tib_next)
			bzero(&tib->tib_data, sizeof tib->tib_data);
	}
}

int
ttyinq_peekchar(struct ttyinq *ti, char *c, int *quote)
{
	unsigned int boff;
	struct ttyinq_block *tib = ti->ti_lastblock;

	if (ti->ti_linestart == ti->ti_end)
		return (-1);

	MPASS(ti->ti_end > 0);
	boff = (ti->ti_end - 1) % TTYINQ_DATASIZE;

	*c = tib->tib_data[boff];
	*quote = GETBIT(tib, boff);

	return (0);
}

void
ttyinq_unputchar(struct ttyinq *ti)
{

	MPASS(ti->ti_linestart < ti->ti_end);

	if (--ti->ti_end % TTYINQ_DATASIZE == 0) {
		/* Roll back to the previous block. */
		ti->ti_lastblock = ti->ti_lastblock->tib_prev;
		/*
		 * This can only fail if we are unputchar()'ing the
		 * first character in the queue.
		 */
		MPASS((ti->ti_lastblock == NULL) == (ti->ti_end == 0));
	}
}

void
ttyinq_reprintpos_set(struct ttyinq *ti)
{

	ti->ti_reprint = ti->ti_end;
	ti->ti_reprintblock = ti->ti_lastblock;
}

void
ttyinq_reprintpos_reset(struct ttyinq *ti)
{

	ti->ti_reprint = ti->ti_linestart;
	ti->ti_reprintblock = ti->ti_startblock;
}

static void
ttyinq_line_iterate(struct ttyinq *ti,
    ttyinq_line_iterator_t *iterator, void *data,
    unsigned int offset, struct ttyinq_block *tib)
{
	unsigned int boff;

	/* Use the proper block when we're at the queue head. */
	if (offset == 0)
		tib = ti->ti_firstblock;

	/* Iterate all characters and call the iterator function. */
	for (; offset < ti->ti_end; offset++) {
		boff = offset % TTYINQ_DATASIZE;
		MPASS(tib != NULL);

		/* Call back the iterator function. */
		iterator(data, tib->tib_data[boff], GETBIT(tib, boff));

		/* Last byte iterated - go to the next block. */
		if (boff == TTYINQ_DATASIZE - 1)
			tib = tib->tib_next;
		MPASS(tib != NULL);
	}
}

void
ttyinq_line_iterate_from_linestart(struct ttyinq *ti,
    ttyinq_line_iterator_t *iterator, void *data)
{

	ttyinq_line_iterate(ti, iterator, data,
	    ti->ti_linestart, ti->ti_startblock);
}

void
ttyinq_line_iterate_from_reprintpos(struct ttyinq *ti,
    ttyinq_line_iterator_t *iterator, void *data)
{

	ttyinq_line_iterate(ti, iterator, data,
	    ti->ti_reprint, ti->ti_reprintblock);
}

static void
ttyinq_startup(void *dummy)
{

	ttyinq_zone = uma_zcreate("ttyinq", sizeof(struct ttyinq_block),
	    NULL, NULL, NULL, NULL, UMA_ALIGN_PTR, 0);
}

SYSINIT(ttyinq, SI_SUB_DRIVERS, SI_ORDER_FIRST, ttyinq_startup, NULL);
