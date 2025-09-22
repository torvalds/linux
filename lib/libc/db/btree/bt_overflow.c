/*	$OpenBSD: bt_overflow.c,v 1.11 2015/01/16 16:48:51 deraadt Exp $	*/

/*-
 * Copyright (c) 1990, 1993, 1994
 *	The Regents of the University of California.  All rights reserved.
 *
 * This code is derived from software contributed to Berkeley by
 * Mike Olson.
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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <db.h>
#include "btree.h"

#define MINIMUM(a, b)	(((a) < (b)) ? (a) : (b))

/*
 * Big key/data code.
 *
 * Big key and data entries are stored on linked lists of pages.  The initial
 * reference is byte string stored with the key or data and is the page number
 * and size.  The actual record is stored in a chain of pages linked by the
 * nextpg field of the PAGE header.
 *
 * The first page of the chain has a special property.  If the record is used
 * by an internal page, it cannot be deleted and the P_PRESERVE bit will be set
 * in the header.
 *
 * XXX
 * A single DBT is written to each chain, so a lot of space on the last page
 * is wasted.  This is a fairly major bug for some data sets.
 */

/*
 * __OVFL_GET -- Get an overflow key/data item.
 *
 * Parameters:
 *	t:	tree
 *	p:	pointer to { pgno_t, u_int32_t }
 *	buf:	storage address
 *	bufsz:	storage size
 *
 * Returns:
 *	RET_ERROR, RET_SUCCESS
 */
int
__ovfl_get(BTREE *t, void *p, size_t *ssz, void **buf, size_t *bufsz)
{
	PAGE *h;
	pgno_t pg;
	size_t nb, plen;
	u_int32_t sz;
	void *tp;

	memmove(&pg, p, sizeof(pgno_t));
	memmove(&sz, (char *)p + sizeof(pgno_t), sizeof(u_int32_t));
	*ssz = sz;

#ifdef DEBUG
	if (pg == P_INVALID || sz == 0)
		abort();
#endif
	/* Make the buffer bigger as necessary. */
	if (*bufsz < sz) {
		tp = realloc(*buf, sz);
		if (tp == NULL)
			return (RET_ERROR);
		*buf = tp;
		*bufsz = sz;
	}

	/*
	 * Step through the linked list of pages, copying the data on each one
	 * into the buffer.  Never copy more than the data's length.
	 */
	plen = t->bt_psize - BTDATAOFF;
	for (p = *buf;; p = (char *)p + nb, pg = h->nextpg) {
		if ((h = mpool_get(t->bt_mp, pg, 0)) == NULL)
			return (RET_ERROR);

		nb = MINIMUM(sz, plen);
		memmove(p, (char *)h + BTDATAOFF, nb);
		mpool_put(t->bt_mp, h, 0);

		if ((sz -= nb) == 0)
			break;
	}
	return (RET_SUCCESS);
}

/*
 * __OVFL_PUT -- Store an overflow key/data item.
 *
 * Parameters:
 *	t:	tree
 *	data:	DBT to store
 *	pgno:	storage page number
 *
 * Returns:
 *	RET_ERROR, RET_SUCCESS
 */
int
__ovfl_put(BTREE *t, const DBT *dbt, pgno_t *pg)
{
	PAGE *h, *last;
	void *p;
	pgno_t npg;
	size_t nb, plen;
	u_int32_t sz;

	/*
	 * Allocate pages and copy the key/data record into them.  Store the
	 * number of the first page in the chain.
	 */
	plen = t->bt_psize - BTDATAOFF;
	for (last = NULL, p = dbt->data, sz = dbt->size;;
	    p = (char *)p + plen, last = h) {
		if ((h = __bt_new(t, &npg)) == NULL)
			return (RET_ERROR);

		h->pgno = npg;
		h->nextpg = h->prevpg = P_INVALID;
		h->flags = P_OVERFLOW;
		h->lower = h->upper = 0;

		nb = MINIMUM(sz, plen);
		memmove((char *)h + BTDATAOFF, p, nb);

		if (last) {
			last->nextpg = h->pgno;
			mpool_put(t->bt_mp, last, MPOOL_DIRTY);
		} else
			*pg = h->pgno;

		if ((sz -= nb) == 0) {
			mpool_put(t->bt_mp, h, MPOOL_DIRTY);
			break;
		}
	}
	return (RET_SUCCESS);
}

/*
 * __OVFL_DELETE -- Delete an overflow chain.
 *
 * Parameters:
 *	t:	tree
 *	p:	pointer to { pgno_t, u_int32_t }
 *
 * Returns:
 *	RET_ERROR, RET_SUCCESS
 */
int
__ovfl_delete(BTREE *t, void *p)
{
	PAGE *h;
	pgno_t pg;
	size_t plen;
	u_int32_t sz;

	memmove(&pg, p, sizeof(pgno_t));
	memmove(&sz, (char *)p + sizeof(pgno_t), sizeof(u_int32_t));

#ifdef DEBUG
	if (pg == P_INVALID || sz == 0)
		abort();
#endif
	if ((h = mpool_get(t->bt_mp, pg, 0)) == NULL)
		return (RET_ERROR);

	/* Don't delete chains used by internal pages. */
	if (h->flags & P_PRESERVE) {
		mpool_put(t->bt_mp, h, 0);
		return (RET_SUCCESS);
	}

	/* Step through the chain, calling the free routine for each page. */
	for (plen = t->bt_psize - BTDATAOFF;; sz -= plen) {
		pg = h->nextpg;
		__bt_free(t, h);
		if (sz <= plen)
			break;
		if ((h = mpool_get(t->bt_mp, pg, 0)) == NULL)
			return (RET_ERROR);
	}
	return (RET_SUCCESS);
}
