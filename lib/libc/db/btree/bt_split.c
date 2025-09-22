/*	$OpenBSD: bt_split.c,v 1.13 2005/08/05 13:03:00 espie Exp $	*/

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

#include <sys/types.h>

#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <db.h>
#include "btree.h"

static int	 bt_broot(BTREE *, PAGE *, PAGE *, PAGE *);
static PAGE	*bt_page(BTREE *, PAGE *, PAGE **, PAGE **, indx_t *, size_t);
static int	 bt_preserve(BTREE *, pgno_t);
static PAGE	*bt_psplit(BTREE *, PAGE *, PAGE *, PAGE *, indx_t *, size_t);
static PAGE	*bt_root(BTREE *, PAGE *, PAGE **, PAGE **, indx_t *, size_t);
static int	 bt_rroot(BTREE *, PAGE *, PAGE *, PAGE *);
static recno_t	 rec_total(PAGE *);

#ifdef STATISTICS
u_long	bt_rootsplit, bt_split, bt_sortsplit, bt_pfxsaved;
#endif

/*
 * __BT_SPLIT -- Split the tree.
 *
 * Parameters:
 *	t:	tree
 *	sp:	page to split
 *	key:	key to insert
 *	data:	data to insert
 *	flags:	BIGKEY/BIGDATA flags
 *	ilen:	insert length
 *	skip:	index to leave open
 *
 * Returns:
 *	RET_ERROR, RET_SUCCESS
 */
int
__bt_split(BTREE *t, PAGE *sp, const DBT *key, const DBT *data, int flags,
    size_t ilen, u_int32_t argskip)
{
	BINTERNAL *bi;
	BLEAF *bl, *tbl;
	DBT a, b;
	EPGNO *parent;
	PAGE *h, *l, *r, *lchild, *rchild;
	indx_t nxtindex;
	u_int16_t skip;
	u_int32_t n, nbytes, nksize;
	int parentsplit;
	char *dest;

	/*
	 * Split the page into two pages, l and r.  The split routines return
	 * a pointer to the page into which the key should be inserted and with
	 * skip set to the offset which should be used.  Additionally, l and r
	 * are pinned.
	 */
	skip = argskip;
	h = sp->pgno == P_ROOT ?
	    bt_root(t, sp, &l, &r, &skip, ilen) :
	    bt_page(t, sp, &l, &r, &skip, ilen);
	if (h == NULL)
		return (RET_ERROR);

	/*
	 * Insert the new key/data pair into the leaf page.  (Key inserts
	 * always cause a leaf page to split first.)
	 */
	h->linp[skip] = h->upper -= ilen;
	dest = (char *)h + h->upper;
	if (F_ISSET(t, R_RECNO))
		WR_RLEAF(dest, data, flags)
	else
		WR_BLEAF(dest, key, data, flags)

	/* If the root page was split, make it look right. */
	if (sp->pgno == P_ROOT &&
	    (F_ISSET(t, R_RECNO) ?
	    bt_rroot(t, sp, l, r) : bt_broot(t, sp, l, r)) == RET_ERROR)
		goto err2;

	/*
	 * Now we walk the parent page stack -- a LIFO stack of the pages that
	 * were traversed when we searched for the page that split.  Each stack
	 * entry is a page number and a page index offset.  The offset is for
	 * the page traversed on the search.  We've just split a page, so we
	 * have to insert a new key into the parent page.
	 *
	 * If the insert into the parent page causes it to split, may have to
	 * continue splitting all the way up the tree.  We stop if the root
	 * splits or the page inserted into didn't have to split to hold the
	 * new key.  Some algorithms replace the key for the old page as well
	 * as the new page.  We don't, as there's no reason to believe that the
	 * first key on the old page is any better than the key we have, and,
	 * in the case of a key being placed at index 0 causing the split, the
	 * key is unavailable.
	 *
	 * There are a maximum of 5 pages pinned at any time.  We keep the left
	 * and right pages pinned while working on the parent.   The 5 are the
	 * two children, left parent and right parent (when the parent splits)
	 * and the root page or the overflow key page when calling bt_preserve.
	 * This code must make sure that all pins are released other than the
	 * root page or overflow page which is unlocked elsewhere.
	 */
	while ((parent = BT_POP(t)) != NULL) {
		lchild = l;
		rchild = r;

		/* Get the parent page. */
		if ((h = mpool_get(t->bt_mp, parent->pgno, 0)) == NULL)
			goto err2;

	 	/*
		 * The new key goes ONE AFTER the index, because the split
		 * was to the right.
		 */
		skip = parent->index + 1;

		/*
		 * Calculate the space needed on the parent page.
		 *
		 * Prefix trees: space hack when inserting into BINTERNAL
		 * pages.  Retain only what's needed to distinguish between
		 * the new entry and the LAST entry on the page to its left.
		 * If the keys compare equal, retain the entire key.  Note,
		 * we don't touch overflow keys, and the entire key must be
		 * retained for the next-to-left most key on the leftmost
		 * page of each level, or the search will fail.  Applicable
		 * ONLY to internal pages that have leaf pages as children.
		 * Further reduction of the key between pairs of internal
		 * pages loses too much information.
		 */
		switch (rchild->flags & P_TYPE) {
		case P_BINTERNAL:
			bi = GETBINTERNAL(rchild, 0);
			nbytes = NBINTERNAL(bi->ksize);
			break;
		case P_BLEAF:
			bl = GETBLEAF(rchild, 0);
			nbytes = NBINTERNAL(bl->ksize);
			if (t->bt_pfx && !(bl->flags & P_BIGKEY) &&
			    (h->prevpg != P_INVALID || skip > 1)) {
				tbl = GETBLEAF(lchild, NEXTINDEX(lchild) - 1);
				a.size = tbl->ksize;
				a.data = tbl->bytes;
				b.size = bl->ksize;
				b.data = bl->bytes;
				nksize = t->bt_pfx(&a, &b);
				n = NBINTERNAL(nksize);
				if (n < nbytes) {
#ifdef STATISTICS
					bt_pfxsaved += nbytes - n;
#endif
					nbytes = n;
				} else
					nksize = 0;
			} else
				nksize = 0;
			break;
		case P_RINTERNAL:
		case P_RLEAF:
			nbytes = NRINTERNAL;
			break;
		default:
			abort();
		}

		/* Split the parent page if necessary or shift the indices. */
		if (h->upper - h->lower < nbytes + sizeof(indx_t)) {
			sp = h;
			h = h->pgno == P_ROOT ?
			    bt_root(t, h, &l, &r, &skip, nbytes) :
			    bt_page(t, h, &l, &r, &skip, nbytes);
			if (h == NULL)
				goto err1;
			parentsplit = 1;
		} else {
			if (skip < (nxtindex = NEXTINDEX(h)))
				memmove(h->linp + skip + 1, h->linp + skip,
				    (nxtindex - skip) * sizeof(indx_t));
			h->lower += sizeof(indx_t);
			parentsplit = 0;
		}

		/* Insert the key into the parent page. */
		switch (rchild->flags & P_TYPE) {
		case P_BINTERNAL:
			h->linp[skip] = h->upper -= nbytes;
			dest = (char *)h + h->linp[skip];
			memmove(dest, bi, nbytes);
			((BINTERNAL *)dest)->pgno = rchild->pgno;
			break;
		case P_BLEAF:
			h->linp[skip] = h->upper -= nbytes;
			dest = (char *)h + h->linp[skip];
			WR_BINTERNAL(dest, nksize ? nksize : bl->ksize,
			    rchild->pgno, bl->flags & P_BIGKEY);
			memmove(dest, bl->bytes, nksize ? nksize : bl->ksize);
			if (bl->flags & P_BIGKEY &&
			    bt_preserve(t, *(pgno_t *)bl->bytes) == RET_ERROR)
				goto err1;
			break;
		case P_RINTERNAL:
			/*
			 * Update the left page count.  If split
			 * added at index 0, fix the correct page.
			 */
			if (skip > 0)
				dest = (char *)h + h->linp[skip - 1];
			else
				dest = (char *)l + l->linp[NEXTINDEX(l) - 1];
			((RINTERNAL *)dest)->nrecs = rec_total(lchild);
			((RINTERNAL *)dest)->pgno = lchild->pgno;

			/* Update the right page count. */
			h->linp[skip] = h->upper -= nbytes;
			dest = (char *)h + h->linp[skip];
			((RINTERNAL *)dest)->nrecs = rec_total(rchild);
			((RINTERNAL *)dest)->pgno = rchild->pgno;
			break;
		case P_RLEAF:
			/*
			 * Update the left page count.  If split
			 * added at index 0, fix the correct page.
			 */
			if (skip > 0)
				dest = (char *)h + h->linp[skip - 1];
			else
				dest = (char *)l + l->linp[NEXTINDEX(l) - 1];
			((RINTERNAL *)dest)->nrecs = NEXTINDEX(lchild);
			((RINTERNAL *)dest)->pgno = lchild->pgno;

			/* Update the right page count. */
			h->linp[skip] = h->upper -= nbytes;
			dest = (char *)h + h->linp[skip];
			((RINTERNAL *)dest)->nrecs = NEXTINDEX(rchild);
			((RINTERNAL *)dest)->pgno = rchild->pgno;
			break;
		default:
			abort();
		}

		/* Unpin the held pages. */
		if (!parentsplit) {
			mpool_put(t->bt_mp, h, MPOOL_DIRTY);
			break;
		}

		/* If the root page was split, make it look right. */
		if (sp->pgno == P_ROOT &&
		    (F_ISSET(t, R_RECNO) ?
		    bt_rroot(t, sp, l, r) : bt_broot(t, sp, l, r)) == RET_ERROR)
			goto err1;

		mpool_put(t->bt_mp, lchild, MPOOL_DIRTY);
		mpool_put(t->bt_mp, rchild, MPOOL_DIRTY);
	}

	/* Unpin the held pages. */
	mpool_put(t->bt_mp, l, MPOOL_DIRTY);
	mpool_put(t->bt_mp, r, MPOOL_DIRTY);

	/* Clear any pages left on the stack. */
	return (RET_SUCCESS);

	/*
	 * If something fails in the above loop we were already walking back
	 * up the tree and the tree is now inconsistent.  Nothing much we can
	 * do about it but release any memory we're holding.
	 */
err1:	mpool_put(t->bt_mp, lchild, MPOOL_DIRTY);
	mpool_put(t->bt_mp, rchild, MPOOL_DIRTY);

err2:	mpool_put(t->bt_mp, l, 0);
	mpool_put(t->bt_mp, r, 0);
	__dbpanic(t->bt_dbp);
	return (RET_ERROR);
}

/*
 * BT_PAGE -- Split a non-root page of a btree.
 *
 * Parameters:
 *	t:	tree
 *	h:	root page
 *	lp:	pointer to left page pointer
 *	rp:	pointer to right page pointer
 *	skip:	pointer to index to leave open
 *	ilen:	insert length
 *
 * Returns:
 *	Pointer to page in which to insert or NULL on error.
 */
static PAGE *
bt_page(BTREE *t, PAGE *h, PAGE **lp, PAGE **rp, indx_t *skip, size_t ilen)
{
	PAGE *l, *r, *tp;
	pgno_t npg;

#ifdef STATISTICS
	++bt_split;
#endif
	/* Put the new right page for the split into place. */
	if ((r = __bt_new(t, &npg)) == NULL)
		return (NULL);
	r->pgno = npg;
	r->lower = BTDATAOFF;
	r->upper = t->bt_psize;
	r->nextpg = h->nextpg;
	r->prevpg = h->pgno;
	r->flags = h->flags & P_TYPE;

	/*
	 * If we're splitting the last page on a level because we're appending
	 * a key to it (skip is NEXTINDEX()), it's likely that the data is
	 * sorted.  Adding an empty page on the side of the level is less work
	 * and can push the fill factor much higher than normal.  If we're
	 * wrong it's no big deal, we'll just do the split the right way next
	 * time.  It may look like it's equally easy to do a similar hack for
	 * reverse sorted data, that is, split the tree left, but it's not.
	 * Don't even try.
	 */
	if (h->nextpg == P_INVALID && *skip == NEXTINDEX(h)) {
#ifdef STATISTICS
		++bt_sortsplit;
#endif
		h->nextpg = r->pgno;
		r->lower = BTDATAOFF + sizeof(indx_t);
		*skip = 0;
		*lp = h;
		*rp = r;
		return (r);
	}

	/* Put the new left page for the split into place. */
	if ((l = (PAGE *)malloc(t->bt_psize)) == NULL) {
		mpool_put(t->bt_mp, r, 0);
		return (NULL);
	}
	memset(l, 0xff, t->bt_psize);
	l->pgno = h->pgno;
	l->nextpg = r->pgno;
	l->prevpg = h->prevpg;
	l->lower = BTDATAOFF;
	l->upper = t->bt_psize;
	l->flags = h->flags & P_TYPE;

	/* Fix up the previous pointer of the page after the split page. */
	if (h->nextpg != P_INVALID) {
		if ((tp = mpool_get(t->bt_mp, h->nextpg, 0)) == NULL) {
			free(l);
			/* XXX mpool_free(t->bt_mp, r->pgno); */
			return (NULL);
		}
		tp->prevpg = r->pgno;
		mpool_put(t->bt_mp, tp, MPOOL_DIRTY);
	}

	/*
	 * Split right.  The key/data pairs aren't sorted in the btree page so
	 * it's simpler to copy the data from the split page onto two new pages
	 * instead of copying half the data to the right page and compacting
	 * the left page in place.  Since the left page can't change, we have
	 * to swap the original and the allocated left page after the split.
	 */
	tp = bt_psplit(t, h, l, r, skip, ilen);

	/* Move the new left page onto the old left page. */
	memmove(h, l, t->bt_psize);
	if (tp == l)
		tp = h;
	free(l);

	*lp = h;
	*rp = r;
	return (tp);
}

/*
 * BT_ROOT -- Split the root page of a btree.
 *
 * Parameters:
 *	t:	tree
 *	h:	root page
 *	lp:	pointer to left page pointer
 *	rp:	pointer to right page pointer
 *	skip:	pointer to index to leave open
 *	ilen:	insert length
 *
 * Returns:
 *	Pointer to page in which to insert or NULL on error.
 */
static PAGE *
bt_root(BTREE *t, PAGE *h, PAGE **lp, PAGE **rp, indx_t *skip, size_t ilen)
{
	PAGE *l, *r, *tp;
	pgno_t lnpg, rnpg;

#ifdef STATISTICS
	++bt_split;
	++bt_rootsplit;
#endif
	/* Put the new left and right pages for the split into place. */
	if ((l = __bt_new(t, &lnpg)) == NULL ||
	    (r = __bt_new(t, &rnpg)) == NULL)
		return (NULL);
	l->pgno = lnpg;
	r->pgno = rnpg;
	l->nextpg = r->pgno;
	r->prevpg = l->pgno;
	l->prevpg = r->nextpg = P_INVALID;
	l->lower = r->lower = BTDATAOFF;
	l->upper = r->upper = t->bt_psize;
	l->flags = r->flags = h->flags & P_TYPE;

	/* Split the root page. */
	tp = bt_psplit(t, h, l, r, skip, ilen);

	*lp = l;
	*rp = r;
	return (tp);
}

/*
 * BT_RROOT -- Fix up the recno root page after it has been split.
 *
 * Parameters:
 *	t:	tree
 *	h:	root page
 *	l:	left page
 *	r:	right page
 *
 * Returns:
 *	RET_ERROR, RET_SUCCESS
 */
static int
bt_rroot(BTREE *t, PAGE *h, PAGE *l, PAGE *r)
{
	char *dest;

	/* Insert the left and right keys, set the header information. */
	h->linp[0] = h->upper = t->bt_psize - NRINTERNAL;
	dest = (char *)h + h->upper;
	WR_RINTERNAL(dest,
	    l->flags & P_RLEAF ? NEXTINDEX(l) : rec_total(l), l->pgno);

	h->linp[1] = h->upper -= NRINTERNAL;
	dest = (char *)h + h->upper;
	WR_RINTERNAL(dest,
	    r->flags & P_RLEAF ? NEXTINDEX(r) : rec_total(r), r->pgno);

	h->lower = BTDATAOFF + 2 * sizeof(indx_t);

	/* Unpin the root page, set to recno internal page. */
	h->flags &= ~P_TYPE;
	h->flags |= P_RINTERNAL;
	mpool_put(t->bt_mp, h, MPOOL_DIRTY);

	return (RET_SUCCESS);
}

/*
 * BT_BROOT -- Fix up the btree root page after it has been split.
 *
 * Parameters:
 *	t:	tree
 *	h:	root page
 *	l:	left page
 *	r:	right page
 *
 * Returns:
 *	RET_ERROR, RET_SUCCESS
 */
static int
bt_broot(BTREE *t, PAGE *h, PAGE *l, PAGE *r)
{
	BINTERNAL *bi;
	BLEAF *bl;
	u_int32_t nbytes;
	char *dest;

	/*
	 * If the root page was a leaf page, change it into an internal page.
	 * We copy the key we split on (but not the key's data, in the case of
	 * a leaf page) to the new root page.
	 *
	 * The btree comparison code guarantees that the left-most key on any
	 * level of the tree is never used, so it doesn't need to be filled in.
	 */
	nbytes = NBINTERNAL(0);
	h->linp[0] = h->upper = t->bt_psize - nbytes;
	dest = (char *)h + h->upper;
	WR_BINTERNAL(dest, 0, l->pgno, 0);

	switch (h->flags & P_TYPE) {
	case P_BLEAF:
		bl = GETBLEAF(r, 0);
		nbytes = NBINTERNAL(bl->ksize);
		h->linp[1] = h->upper -= nbytes;
		dest = (char *)h + h->upper;
		WR_BINTERNAL(dest, bl->ksize, r->pgno, 0);
		memmove(dest, bl->bytes, bl->ksize);

		/*
		 * If the key is on an overflow page, mark the overflow chain
		 * so it isn't deleted when the leaf copy of the key is deleted.
		 */
		if (bl->flags & P_BIGKEY &&
		    bt_preserve(t, *(pgno_t *)bl->bytes) == RET_ERROR)
			return (RET_ERROR);
		break;
	case P_BINTERNAL:
		bi = GETBINTERNAL(r, 0);
		nbytes = NBINTERNAL(bi->ksize);
		h->linp[1] = h->upper -= nbytes;
		dest = (char *)h + h->upper;
		memmove(dest, bi, nbytes);
		((BINTERNAL *)dest)->pgno = r->pgno;
		break;
	default:
		abort();
	}

	/* There are two keys on the page. */
	h->lower = BTDATAOFF + 2 * sizeof(indx_t);

	/* Unpin the root page, set to btree internal page. */
	h->flags &= ~P_TYPE;
	h->flags |= P_BINTERNAL;
	mpool_put(t->bt_mp, h, MPOOL_DIRTY);

	return (RET_SUCCESS);
}

/*
 * BT_PSPLIT -- Do the real work of splitting the page.
 *
 * Parameters:
 *	t:	tree
 *	h:	page to be split
 *	l:	page to put lower half of data
 *	r:	page to put upper half of data
 *	pskip:	pointer to index to leave open
 *	ilen:	insert length
 *
 * Returns:
 *	Pointer to page in which to insert.
 */
static PAGE *
bt_psplit(BTREE *t, PAGE *h, PAGE *l, PAGE *r, indx_t *pskip, size_t ilen)
{
	BINTERNAL *bi;
	BLEAF *bl;
	CURSOR *c;
	RLEAF *rl;
	PAGE *rval;
	void *src;
	indx_t full, half, nxt, off, skip, top, used;
	u_int32_t nbytes;
	int bigkeycnt, isbigkey;

	/*
	 * Split the data to the left and right pages.  Leave the skip index
	 * open.  Additionally, make some effort not to split on an overflow
	 * key.  This makes internal page processing faster and can save
	 * space as overflow keys used by internal pages are never deleted.
	 */
	bigkeycnt = 0;
	skip = *pskip;
	full = t->bt_psize - BTDATAOFF;
	half = full / 2;
	used = 0;
	for (nxt = off = 0, top = NEXTINDEX(h); nxt < top; ++off) {
		if (skip == off) {
			nbytes = ilen;
			isbigkey = 0;		/* XXX: not really known. */
		} else
			switch (h->flags & P_TYPE) {
			case P_BINTERNAL:
				src = bi = GETBINTERNAL(h, nxt);
				nbytes = NBINTERNAL(bi->ksize);
				isbigkey = bi->flags & P_BIGKEY;
				break;
			case P_BLEAF:
				src = bl = GETBLEAF(h, nxt);
				nbytes = NBLEAF(bl);
				isbigkey = bl->flags & P_BIGKEY;
				break;
			case P_RINTERNAL:
				src = GETRINTERNAL(h, nxt);
				nbytes = NRINTERNAL;
				isbigkey = 0;
				break;
			case P_RLEAF:
				src = rl = GETRLEAF(h, nxt);
				nbytes = NRLEAF(rl);
				isbigkey = 0;
				break;
			default:
				abort();
			}

		/*
		 * If the key/data pairs are substantial fractions of the max
		 * possible size for the page, it's possible to get situations
		 * where we decide to try and copy too much onto the left page.
		 * Make sure that doesn't happen.
		 */
		if ((skip <= off && used + nbytes + sizeof(indx_t) >= full) ||
		    nxt == top - 1) {
			--off;
			break;
		}

		/* Copy the key/data pair, if not the skipped index. */
		if (skip != off) {
			++nxt;

			l->linp[off] = l->upper -= nbytes;
			memmove((char *)l + l->upper, src, nbytes);
		}

		used += nbytes + sizeof(indx_t);
		if (used >= half) {
			if (!isbigkey || bigkeycnt == 3)
				break;
			else
				++bigkeycnt;
		}
	}

	/*
	 * Off is the last offset that's valid for the left page.
	 * Nxt is the first offset to be placed on the right page.
	 */
	l->lower += (off + 1) * sizeof(indx_t);

	/*
	 * If splitting the page that the cursor was on, the cursor has to be
	 * adjusted to point to the same record as before the split.  If the
	 * cursor is at or past the skipped slot, the cursor is incremented by
	 * one.  If the cursor is on the right page, it is decremented by the
	 * number of records split to the left page.
	 */
	c = &t->bt_cursor;
	if (F_ISSET(c, CURS_INIT) && c->pg.pgno == h->pgno) {
		if (c->pg.index >= skip)
			++c->pg.index;
		if (c->pg.index < nxt)			/* Left page. */
			c->pg.pgno = l->pgno;
		else {					/* Right page. */
			c->pg.pgno = r->pgno;
			c->pg.index -= nxt;
		}
	}

	/*
	 * If the skipped index was on the left page, just return that page.
	 * Otherwise, adjust the skip index to reflect the new position on
	 * the right page.
	 */
	if (skip <= off) {
		skip = MAX_PAGE_OFFSET;
		rval = l;
	} else {
		rval = r;
		*pskip -= nxt;
	}

	for (off = 0; nxt < top; ++off) {
		if (skip == nxt) {
			++off;
			skip = MAX_PAGE_OFFSET;
		}
		switch (h->flags & P_TYPE) {
		case P_BINTERNAL:
			src = bi = GETBINTERNAL(h, nxt);
			nbytes = NBINTERNAL(bi->ksize);
			break;
		case P_BLEAF:
			src = bl = GETBLEAF(h, nxt);
			nbytes = NBLEAF(bl);
			break;
		case P_RINTERNAL:
			src = GETRINTERNAL(h, nxt);
			nbytes = NRINTERNAL;
			break;
		case P_RLEAF:
			src = rl = GETRLEAF(h, nxt);
			nbytes = NRLEAF(rl);
			break;
		default:
			abort();
		}
		++nxt;
		r->linp[off] = r->upper -= nbytes;
		memmove((char *)r + r->upper, src, nbytes);
	}
	r->lower += off * sizeof(indx_t);

	/* If the key is being appended to the page, adjust the index. */
	if (skip == top)
		r->lower += sizeof(indx_t);

	return (rval);
}

/*
 * BT_PRESERVE -- Mark a chain of pages as used by an internal node.
 *
 * Chains of indirect blocks pointed to by leaf nodes get reclaimed when the
 * record that references them gets deleted.  Chains pointed to by internal
 * pages never get deleted.  This routine marks a chain as pointed to by an
 * internal page.
 *
 * Parameters:
 *	t:	tree
 *	pg:	page number of first page in the chain.
 *
 * Returns:
 *	RET_SUCCESS, RET_ERROR.
 */
static int
bt_preserve(BTREE *t, pgno_t pg)
{
	PAGE *h;

	if ((h = mpool_get(t->bt_mp, pg, 0)) == NULL)
		return (RET_ERROR);
	h->flags |= P_PRESERVE;
	mpool_put(t->bt_mp, h, MPOOL_DIRTY);
	return (RET_SUCCESS);
}

/*
 * REC_TOTAL -- Return the number of recno entries below a page.
 *
 * Parameters:
 *	h:	page
 *
 * Returns:
 *	The number of recno entries below a page.
 *
 * XXX
 * These values could be set by the bt_psplit routine.  The problem is that the
 * entry has to be popped off of the stack etc. or the values have to be passed
 * all the way back to bt_split/bt_rroot and it's not very clean.
 */
static recno_t
rec_total(PAGE *h)
{
	recno_t recs;
	indx_t nxt, top;

	for (recs = 0, nxt = 0, top = NEXTINDEX(h); nxt < top; ++nxt)
		recs += GETRINTERNAL(h, nxt)->nrecs;
	return (recs);
}
