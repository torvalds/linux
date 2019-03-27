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
#include <sys/systm.h>
#include <sys/tty.h>
#include <sys/uio.h>

#include <vm/uma.h>

/*
 * TTY output queue buffering.
 *
 * The previous design of the TTY layer offered the so-called clists.
 * These clists were used for both the input queues and the output
 * queue. We don't use certain features on the output side, like quoting
 * bits for parity marking and such. This mechanism is similar to the
 * old clists, but only contains the features we need to buffer the
 * output.
 */

struct ttyoutq_block {
	struct ttyoutq_block	*tob_next;
	char			tob_data[TTYOUTQ_DATASIZE];
};

static uma_zone_t ttyoutq_zone;

#define	TTYOUTQ_INSERT_TAIL(to, tob) do {				\
	if (to->to_end == 0) {						\
		tob->tob_next = to->to_firstblock;			\
		to->to_firstblock = tob;				\
	} else {							\
		tob->tob_next = to->to_lastblock->tob_next;		\
		to->to_lastblock->tob_next = tob;			\
	}								\
	to->to_nblocks++;						\
} while (0)

#define	TTYOUTQ_REMOVE_HEAD(to) do {					\
	to->to_firstblock = to->to_firstblock->tob_next;		\
	to->to_nblocks--;						\
} while (0)

#define	TTYOUTQ_RECYCLE(to, tob) do {					\
	if (to->to_quota <= to->to_nblocks)				\
		uma_zfree(ttyoutq_zone, tob);				\
	else								\
		TTYOUTQ_INSERT_TAIL(to, tob);				\
} while(0)

void
ttyoutq_flush(struct ttyoutq *to)
{

	to->to_begin = 0;
	to->to_end = 0;
}

int
ttyoutq_setsize(struct ttyoutq *to, struct tty *tp, size_t size)
{
	struct ttyoutq_block *tob;

	to->to_quota = howmany(size, TTYOUTQ_DATASIZE);

	while (to->to_quota > to->to_nblocks) {
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
		tob = uma_zalloc(ttyoutq_zone, M_WAITOK);
		tty_lock(tp);

		if (tty_gone(tp)) {
			uma_zfree(ttyoutq_zone, tob);
			return (ENXIO);
		}

		TTYOUTQ_INSERT_TAIL(to, tob);
	}
	return (0);
}

void
ttyoutq_free(struct ttyoutq *to)
{
	struct ttyoutq_block *tob;

	ttyoutq_flush(to);
	to->to_quota = 0;

	while ((tob = to->to_firstblock) != NULL) {
		TTYOUTQ_REMOVE_HEAD(to);
		uma_zfree(ttyoutq_zone, tob);
	}

	MPASS(to->to_nblocks == 0);
}

size_t
ttyoutq_read(struct ttyoutq *to, void *buf, size_t len)
{
	char *cbuf = buf;

	while (len > 0) {
		struct ttyoutq_block *tob;
		size_t cbegin, cend, clen;

		/* See if there still is data. */
		if (to->to_begin == to->to_end)
			break;
		tob = to->to_firstblock;
		if (tob == NULL)
			break;

		/*
		 * The end address should be the lowest of these three:
		 * - The write pointer
		 * - The blocksize - we can't read beyond the block
		 * - The end address if we could perform the full read
		 */
		cbegin = to->to_begin;
		cend = MIN(MIN(to->to_end, to->to_begin + len),
		    TTYOUTQ_DATASIZE);
		clen = cend - cbegin;

		/* Copy the data out of the buffers. */
		memcpy(cbuf, tob->tob_data + cbegin, clen);
		cbuf += clen;
		len -= clen;

		if (cend == to->to_end) {
			/* Read the complete queue. */
			to->to_begin = 0;
			to->to_end = 0;
		} else if (cend == TTYOUTQ_DATASIZE) {
			/* Read the block until the end. */
			TTYOUTQ_REMOVE_HEAD(to);
			to->to_begin = 0;
			to->to_end -= TTYOUTQ_DATASIZE;
			TTYOUTQ_RECYCLE(to, tob);
		} else {
			/* Read the block partially. */
			to->to_begin += clen;
		}
	}

	return (cbuf - (char *)buf);
}

/*
 * An optimized version of ttyoutq_read() which can be used in pseudo
 * TTY drivers to directly copy data from the outq to userspace, instead
 * of buffering it.
 *
 * We can only copy data directly if we need to read the entire block
 * back to the user, because we temporarily remove the block from the
 * queue. Otherwise we need to copy it to a temporary buffer first, to
 * make sure data remains in the correct order.
 */
int
ttyoutq_read_uio(struct ttyoutq *to, struct tty *tp, struct uio *uio)
{

	while (uio->uio_resid > 0) {
		int error;
		struct ttyoutq_block *tob;
		size_t cbegin, cend, clen;

		/* See if there still is data. */
		if (to->to_begin == to->to_end)
			return (0);
		tob = to->to_firstblock;
		if (tob == NULL)
			return (0);

		/*
		 * The end address should be the lowest of these three:
		 * - The write pointer
		 * - The blocksize - we can't read beyond the block
		 * - The end address if we could perform the full read
		 */
		cbegin = to->to_begin;
		cend = MIN(MIN(to->to_end, to->to_begin + uio->uio_resid),
		    TTYOUTQ_DATASIZE);
		clen = cend - cbegin;

		/*
		 * We can prevent buffering in some cases:
		 * - We need to read the block until the end.
		 * - We don't need to read the block until the end, but
		 *   there is no data beyond it, which allows us to move
		 *   the write pointer to a new block.
		 */
		if (cend == TTYOUTQ_DATASIZE || cend == to->to_end) {
			/*
			 * Fast path: zero copy. Remove the first block,
			 * so we can unlock the TTY temporarily.
			 */
			TTYOUTQ_REMOVE_HEAD(to);
			to->to_begin = 0;
			if (to->to_end <= TTYOUTQ_DATASIZE)
				to->to_end = 0;
			else
				to->to_end -= TTYOUTQ_DATASIZE;

			/* Temporary unlock and copy the data to userspace. */
			tty_unlock(tp);
			error = uiomove(tob->tob_data + cbegin, clen, uio);
			tty_lock(tp);

			/* Block can now be readded to the list. */
			TTYOUTQ_RECYCLE(to, tob);
		} else {
			char ob[TTYOUTQ_DATASIZE - 1];

			/*
			 * Slow path: store data in a temporary buffer.
			 */
			memcpy(ob, tob->tob_data + cbegin, clen);
			to->to_begin += clen;
			MPASS(to->to_begin < TTYOUTQ_DATASIZE);

			/* Temporary unlock and copy the data to userspace. */
			tty_unlock(tp);
			error = uiomove(ob, clen, uio);
			tty_lock(tp);
		}

		if (error != 0)
			return (error);
	}

	return (0);
}

size_t
ttyoutq_write(struct ttyoutq *to, const void *buf, size_t nbytes)
{
	const char *cbuf = buf;
	struct ttyoutq_block *tob;
	unsigned int boff;
	size_t l;

	while (nbytes > 0) {
		boff = to->to_end % TTYOUTQ_DATASIZE;

		if (to->to_end == 0) {
			/* First time we're being used or drained. */
			MPASS(to->to_begin == 0);
			tob = to->to_firstblock;
			if (tob == NULL) {
				/* Queue has no blocks. */
				break;
			}
			to->to_lastblock = tob;
		} else if (boff == 0) {
			/* We reached the end of this block on last write. */
			tob = to->to_lastblock->tob_next;
			if (tob == NULL) {
				/* We've reached the watermark. */
				break;
			}
			to->to_lastblock = tob;
		} else {
			tob = to->to_lastblock;
		}

		/* Don't copy more than was requested. */
		l = MIN(nbytes, TTYOUTQ_DATASIZE - boff);
		MPASS(l > 0);
		memcpy(tob->tob_data + boff, cbuf, l);

		cbuf += l;
		nbytes -= l;
		to->to_end += l;
	}

	return (cbuf - (const char *)buf);
}

int
ttyoutq_write_nofrag(struct ttyoutq *to, const void *buf, size_t nbytes)
{
	size_t ret __unused;

	if (ttyoutq_bytesleft(to) < nbytes)
		return (-1);

	/* We should always be able to write it back. */
	ret = ttyoutq_write(to, buf, nbytes);
	MPASS(ret == nbytes);

	return (0);
}

static void
ttyoutq_startup(void *dummy)
{

	ttyoutq_zone = uma_zcreate("ttyoutq", sizeof(struct ttyoutq_block),
	    NULL, NULL, NULL, NULL, UMA_ALIGN_PTR, 0);
}

SYSINIT(ttyoutq, SI_SUB_DRIVERS, SI_ORDER_FIRST, ttyoutq_startup, NULL);
