/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 1990, 1993, 1994
 *	The Regents of the University of California.  All rights reserved.
 *
 * This code is derived from software contributed to Berkeley by
 * Margo Seltzer.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the University nor the names of its contributors
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
 */

#if defined(LIBC_SCCS) && !defined(lint)
static char sccsid[] = "@(#)hash_buf.c	8.5 (Berkeley) 7/15/94";
#endif /* LIBC_SCCS and not lint */
#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

/*
 * PACKAGE: hash
 *
 * DESCRIPTION:
 *	Contains buffer management
 *
 * ROUTINES:
 * External
 *	__buf_init
 *	__get_buf
 *	__buf_free
 *	__reclaim_buf
 * Internal
 *	newbuf
 */

#include <sys/param.h>

#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef DEBUG
#include <assert.h>
#endif

#include <db.h>
#include "hash.h"
#include "page.h"
#include "extern.h"

static BUFHEAD *newbuf(HTAB *, u_int32_t, BUFHEAD *);

/* Unlink B from its place in the lru */
#define BUF_REMOVE(B) { \
	(B)->prev->next = (B)->next; \
	(B)->next->prev = (B)->prev; \
}

/* Insert B after P */
#define BUF_INSERT(B, P) { \
	(B)->next = (P)->next; \
	(B)->prev = (P); \
	(P)->next = (B); \
	(B)->next->prev = (B); \
}

#define	MRU	hashp->bufhead.next
#define	LRU	hashp->bufhead.prev

#define MRU_INSERT(B)	BUF_INSERT((B), &hashp->bufhead)
#define LRU_INSERT(B)	BUF_INSERT((B), LRU)

/*
 * We are looking for a buffer with address "addr".  If prev_bp is NULL, then
 * address is a bucket index.  If prev_bp is not NULL, then it points to the
 * page previous to an overflow page that we are trying to find.
 *
 * CAVEAT:  The buffer header accessed via prev_bp's ovfl field may no longer
 * be valid.  Therefore, you must always verify that its address matches the
 * address you are seeking.
 */
BUFHEAD *
__get_buf(HTAB *hashp, u_int32_t addr,
    BUFHEAD *prev_bp,	/* If prev_bp set, indicates a new overflow page. */
    int newpage)
{
	BUFHEAD *bp;
	u_int32_t is_disk_mask;
	int is_disk, segment_ndx;
	SEGMENT segp;

	is_disk = 0;
	is_disk_mask = 0;
	if (prev_bp) {
		bp = prev_bp->ovfl;
		if (!bp || (bp->addr != addr))
			bp = NULL;
		if (!newpage)
			is_disk = BUF_DISK;
	} else {
		/* Grab buffer out of directory */
		segment_ndx = addr & (hashp->SGSIZE - 1);

		/* valid segment ensured by __call_hash() */
		segp = hashp->dir[addr >> hashp->SSHIFT];
#ifdef DEBUG
		assert(segp != NULL);
#endif
		bp = PTROF(segp[segment_ndx]);
		is_disk_mask = ISDISK(segp[segment_ndx]);
		is_disk = is_disk_mask || !hashp->new_file;
	}

	if (!bp) {
		bp = newbuf(hashp, addr, prev_bp);
		if (!bp ||
		    __get_page(hashp, bp->page, addr, !prev_bp, is_disk, 0))
			return (NULL);
		if (!prev_bp)
			segp[segment_ndx] =
			    (BUFHEAD *)((intptr_t)bp | is_disk_mask);
	} else {
		BUF_REMOVE(bp);
		MRU_INSERT(bp);
	}
	return (bp);
}

/*
 * We need a buffer for this page. Either allocate one, or evict a resident
 * one (if we have as many buffers as we're allowed) and put this one in.
 *
 * If newbuf finds an error (returning NULL), it also sets errno.
 */
static BUFHEAD *
newbuf(HTAB *hashp, u_int32_t addr, BUFHEAD *prev_bp)
{
	BUFHEAD *bp;		/* The buffer we're going to use */
	BUFHEAD *xbp;		/* Temp pointer */
	BUFHEAD *next_xbp;
	SEGMENT segp;
	int segment_ndx;
	u_int16_t oaddr, *shortp;

	oaddr = 0;
	bp = LRU;

        /* It is bad to overwrite the page under the cursor. */
        if (bp == hashp->cpage) {
                BUF_REMOVE(bp);
                MRU_INSERT(bp);
                bp = LRU;
        }

	/* If prev_bp is part of bp overflow, create a new buffer. */
	if (hashp->nbufs == 0 && prev_bp && bp->ovfl) {
		BUFHEAD *ovfl;

		for (ovfl = bp->ovfl; ovfl ; ovfl = ovfl->ovfl) {
			if (ovfl == prev_bp) {
				hashp->nbufs++;
				break;
			}
		}
	}

	/*
	 * If LRU buffer is pinned, the buffer pool is too small. We need to
	 * allocate more buffers.
	 */
	if (hashp->nbufs || (bp->flags & BUF_PIN) || bp == hashp->cpage) {
		/* Allocate a new one */
		if ((bp = (BUFHEAD *)calloc(1, sizeof(BUFHEAD))) == NULL)
			return (NULL);
		if ((bp->page = (char *)calloc(1, hashp->BSIZE)) == NULL) {
			free(bp);
			return (NULL);
		}
		if (hashp->nbufs)
			hashp->nbufs--;
	} else {
		/* Kick someone out */
		BUF_REMOVE(bp);
		/*
		 * If this is an overflow page with addr 0, it's already been
		 * flushed back in an overflow chain and initialized.
		 */
		if ((bp->addr != 0) || (bp->flags & BUF_BUCKET)) {
			/*
			 * Set oaddr before __put_page so that you get it
			 * before bytes are swapped.
			 */
			shortp = (u_int16_t *)bp->page;
			if (shortp[0])
				oaddr = shortp[shortp[0] - 1];
			if ((bp->flags & BUF_MOD) && __put_page(hashp, bp->page,
			    bp->addr, (int)IS_BUCKET(bp->flags), 0))
				return (NULL);
			/*
			 * Update the pointer to this page (i.e. invalidate it).
			 *
			 * If this is a new file (i.e. we created it at open
			 * time), make sure that we mark pages which have been
			 * written to disk so we retrieve them from disk later,
			 * rather than allocating new pages.
			 */
			if (IS_BUCKET(bp->flags)) {
				segment_ndx = bp->addr & (hashp->SGSIZE - 1);
				segp = hashp->dir[bp->addr >> hashp->SSHIFT];
#ifdef DEBUG
				assert(segp != NULL);
#endif

				if (hashp->new_file &&
				    ((bp->flags & BUF_MOD) ||
				    ISDISK(segp[segment_ndx])))
					segp[segment_ndx] = (BUFHEAD *)BUF_DISK;
				else
					segp[segment_ndx] = NULL;
			}
			/*
			 * Since overflow pages can only be access by means of
			 * their bucket, free overflow pages associated with
			 * this bucket.
			 */
			for (xbp = bp; xbp->ovfl;) {
				next_xbp = xbp->ovfl;
				xbp->ovfl = NULL;
				xbp = next_xbp;

				/* Check that ovfl pointer is up date. */
				if (IS_BUCKET(xbp->flags) ||
				    (oaddr != xbp->addr))
					break;

				shortp = (u_int16_t *)xbp->page;
				if (shortp[0])
					/* set before __put_page */
					oaddr = shortp[shortp[0] - 1];
				if ((xbp->flags & BUF_MOD) && __put_page(hashp,
				    xbp->page, xbp->addr, 0, 0))
					return (NULL);
				xbp->addr = 0;
				xbp->flags = 0;
				BUF_REMOVE(xbp);
				LRU_INSERT(xbp);
			}
		}
	}

	/* Now assign this buffer */
	bp->addr = addr;
#ifdef DEBUG1
	(void)fprintf(stderr, "NEWBUF1: %d->ovfl was %d is now %d\n",
	    bp->addr, (bp->ovfl ? bp->ovfl->addr : 0), 0);
#endif
	bp->ovfl = NULL;
	if (prev_bp) {
		/*
		 * If prev_bp is set, this is an overflow page, hook it in to
		 * the buffer overflow links.
		 */
#ifdef DEBUG1
		(void)fprintf(stderr, "NEWBUF2: %d->ovfl was %d is now %d\n",
		    prev_bp->addr, (prev_bp->ovfl ? prev_bp->ovfl->addr : 0),
		    (bp ? bp->addr : 0));
#endif
		prev_bp->ovfl = bp;
		bp->flags = 0;
	} else
		bp->flags = BUF_BUCKET;
	MRU_INSERT(bp);
	return (bp);
}

void
__buf_init(HTAB *hashp, int nbytes)
{
	BUFHEAD *bfp;
	int npages;

	bfp = &(hashp->bufhead);
	npages = (nbytes + hashp->BSIZE - 1) >> hashp->BSHIFT;
	npages = MAX(npages, MIN_BUFFERS);

	hashp->nbufs = npages;
	bfp->next = bfp;
	bfp->prev = bfp;
	/*
	 * This space is calloc'd so these are already null.
	 *
	 * bfp->ovfl = NULL;
	 * bfp->flags = 0;
	 * bfp->page = NULL;
	 * bfp->addr = 0;
	 */
}

int
__buf_free(HTAB *hashp, int do_free, int to_disk)
{
	BUFHEAD *bp;

	/* Need to make sure that buffer manager has been initialized */
	if (!LRU)
		return (0);
	for (bp = LRU; bp != &hashp->bufhead;) {
		/* Check that the buffer is valid */
		if (bp->addr || IS_BUCKET(bp->flags)) {
			if (to_disk && (bp->flags & BUF_MOD) &&
			    __put_page(hashp, bp->page,
			    bp->addr, IS_BUCKET(bp->flags), 0))
				return (-1);
		}
		/* Check if we are freeing stuff */
		if (do_free) {
			if (bp->page) {
				(void)memset(bp->page, 0, hashp->BSIZE);
				free(bp->page);
			}
			BUF_REMOVE(bp);
			free(bp);
			bp = LRU;
		} else
			bp = bp->prev;
	}
	return (0);
}

void
__reclaim_buf(HTAB *hashp, BUFHEAD *bp)
{
	bp->ovfl = NULL;
	bp->addr = 0;
	bp->flags = 0;
	BUF_REMOVE(bp);
	LRU_INSERT(bp);
}
