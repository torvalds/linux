/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
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

#if defined(LIBC_SCCS) && !defined(lint)
static char sccsid[] = "@(#)bt_delete.c	8.13 (Berkeley) 7/28/94";
#endif /* LIBC_SCCS and not lint */
#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/types.h>

#include <errno.h>
#include <stdio.h>
#include <string.h>

#include <db.h>
#include "btree.h"

static int __bt_bdelete(BTREE *, const DBT *);
static int __bt_curdel(BTREE *, const DBT *, PAGE *, u_int);
static int __bt_pdelete(BTREE *, PAGE *);
static int __bt_relink(BTREE *, PAGE *);
static int __bt_stkacq(BTREE *, PAGE **, CURSOR *);

/*
 * __bt_delete
 *	Delete the item(s) referenced by a key.
 *
 * Return RET_SPECIAL if the key is not found.
 */
int
__bt_delete(const DB *dbp, const DBT *key, u_int flags)
{
	BTREE *t;
	CURSOR *c;
	PAGE *h;
	int status;

	t = dbp->internal;

	/* Toss any page pinned across calls. */
	if (t->bt_pinned != NULL) {
		mpool_put(t->bt_mp, t->bt_pinned, 0);
		t->bt_pinned = NULL;
	}

	/* Check for change to a read-only tree. */
	if (F_ISSET(t, B_RDONLY)) {
		errno = EPERM;
		return (RET_ERROR);
	}

	switch (flags) {
	case 0:
		status = __bt_bdelete(t, key);
		break;
	case R_CURSOR:
		/*
		 * If flags is R_CURSOR, delete the cursor.  Must already
		 * have started a scan and not have already deleted it.
		 */
		c = &t->bt_cursor;
		if (F_ISSET(c, CURS_INIT)) {
			if (F_ISSET(c, CURS_ACQUIRE | CURS_AFTER | CURS_BEFORE))
				return (RET_SPECIAL);
			if ((h = mpool_get(t->bt_mp, c->pg.pgno, 0)) == NULL)
				return (RET_ERROR);

			/*
			 * If the page is about to be emptied, we'll need to
			 * delete it, which means we have to acquire a stack.
			 */
			if (NEXTINDEX(h) == 1)
				if (__bt_stkacq(t, &h, &t->bt_cursor))
					return (RET_ERROR);

			status = __bt_dleaf(t, NULL, h, c->pg.index);

			if (NEXTINDEX(h) == 0 && status == RET_SUCCESS) {
				if (__bt_pdelete(t, h))
					return (RET_ERROR);
			} else
				mpool_put(t->bt_mp,
				    h, status == RET_SUCCESS ? MPOOL_DIRTY : 0);
			break;
		}
		/* FALLTHROUGH */
	default:
		errno = EINVAL;
		return (RET_ERROR);
	}
	if (status == RET_SUCCESS)
		F_SET(t, B_MODIFIED);
	return (status);
}

/*
 * __bt_stkacq --
 *	Acquire a stack so we can delete a cursor entry.
 *
 * Parameters:
 *	  t:	tree
 *	 hp:	pointer to current, pinned PAGE pointer
 *	  c:	pointer to the cursor
 *
 * Returns:
 *	0 on success, 1 on failure
 */
static int
__bt_stkacq(BTREE *t, PAGE **hp, CURSOR *c)
{
	BINTERNAL *bi;
	EPG *e;
	EPGNO *parent;
	PAGE *h;
	indx_t idx;
	pgno_t pgno;
	recno_t nextpg, prevpg;
	int exact, level;

	/*
	 * Find the first occurrence of the key in the tree.  Toss the
	 * currently locked page so we don't hit an already-locked page.
	 */
	h = *hp;
	mpool_put(t->bt_mp, h, 0);
	if ((e = __bt_search(t, &c->key, &exact)) == NULL)
		return (1);
	h = e->page;

	/* See if we got it in one shot. */
	if (h->pgno == c->pg.pgno)
		goto ret;

	/*
	 * Move right, looking for the page.  At each move we have to move
	 * up the stack until we don't have to move to the next page.  If
	 * we have to change pages at an internal level, we have to fix the
	 * stack back up.
	 */
	while (h->pgno != c->pg.pgno) {
		if ((nextpg = h->nextpg) == P_INVALID)
			break;
		mpool_put(t->bt_mp, h, 0);

		/* Move up the stack. */
		for (level = 0; (parent = BT_POP(t)) != NULL; ++level) {
			/* Get the parent page. */
			if ((h = mpool_get(t->bt_mp, parent->pgno, 0)) == NULL)
				return (1);

			/* Move to the next index. */
			if (parent->index != NEXTINDEX(h) - 1) {
				idx = parent->index + 1;
				BT_PUSH(t, h->pgno, idx);
				break;
			}
			mpool_put(t->bt_mp, h, 0);
		}

		/* Restore the stack. */
		while (level--) {
			/* Push the next level down onto the stack. */
			bi = GETBINTERNAL(h, idx);
			pgno = bi->pgno;
			BT_PUSH(t, pgno, 0);

			/* Lose the currently pinned page. */
			mpool_put(t->bt_mp, h, 0);

			/* Get the next level down. */
			if ((h = mpool_get(t->bt_mp, pgno, 0)) == NULL)
				return (1);
			idx = 0;
		}
		mpool_put(t->bt_mp, h, 0);
		if ((h = mpool_get(t->bt_mp, nextpg, 0)) == NULL)
			return (1);
	}

	if (h->pgno == c->pg.pgno)
		goto ret;

	/* Reacquire the original stack. */
	mpool_put(t->bt_mp, h, 0);
	if ((e = __bt_search(t, &c->key, &exact)) == NULL)
		return (1);
	h = e->page;

	/*
	 * Move left, looking for the page.  At each move we have to move
	 * up the stack until we don't have to change pages to move to the
	 * next page.  If we have to change pages at an internal level, we
	 * have to fix the stack back up.
	 */
	while (h->pgno != c->pg.pgno) {
		if ((prevpg = h->prevpg) == P_INVALID)
			break;
		mpool_put(t->bt_mp, h, 0);

		/* Move up the stack. */
		for (level = 0; (parent = BT_POP(t)) != NULL; ++level) {
			/* Get the parent page. */
			if ((h = mpool_get(t->bt_mp, parent->pgno, 0)) == NULL)
				return (1);

			/* Move to the next index. */
			if (parent->index != 0) {
				idx = parent->index - 1;
				BT_PUSH(t, h->pgno, idx);
				break;
			}
			mpool_put(t->bt_mp, h, 0);
		}

		/* Restore the stack. */
		while (level--) {
			/* Push the next level down onto the stack. */
			bi = GETBINTERNAL(h, idx);
			pgno = bi->pgno;

			/* Lose the currently pinned page. */
			mpool_put(t->bt_mp, h, 0);

			/* Get the next level down. */
			if ((h = mpool_get(t->bt_mp, pgno, 0)) == NULL)
				return (1);

			idx = NEXTINDEX(h) - 1;
			BT_PUSH(t, pgno, idx);
		}
		mpool_put(t->bt_mp, h, 0);
		if ((h = mpool_get(t->bt_mp, prevpg, 0)) == NULL)
			return (1);
	}


ret:	mpool_put(t->bt_mp, h, 0);
	return ((*hp = mpool_get(t->bt_mp, c->pg.pgno, 0)) == NULL);
}

/*
 * __bt_bdelete --
 *	Delete all key/data pairs matching the specified key.
 *
 * Parameters:
 *	  t:	tree
 *	key:	key to delete
 *
 * Returns:
 *	RET_ERROR, RET_SUCCESS and RET_SPECIAL if the key not found.
 */
static int
__bt_bdelete(BTREE *t, const DBT *key)
{
	EPG *e;
	PAGE *h;
	int deleted, exact, redo;

	deleted = 0;

	/* Find any matching record; __bt_search pins the page. */
loop:	if ((e = __bt_search(t, key, &exact)) == NULL)
		return (deleted ? RET_SUCCESS : RET_ERROR);
	if (!exact) {
		mpool_put(t->bt_mp, e->page, 0);
		return (deleted ? RET_SUCCESS : RET_SPECIAL);
	}

	/*
	 * Delete forward, then delete backward, from the found key.  If
	 * there are duplicates and we reach either side of the page, do
	 * the key search again, so that we get them all.
	 */
	redo = 0;
	h = e->page;
	do {
		if (__bt_dleaf(t, key, h, e->index)) {
			mpool_put(t->bt_mp, h, 0);
			return (RET_ERROR);
		}
		if (F_ISSET(t, B_NODUPS)) {
			if (NEXTINDEX(h) == 0) {
				if (__bt_pdelete(t, h))
					return (RET_ERROR);
			} else
				mpool_put(t->bt_mp, h, MPOOL_DIRTY);
			return (RET_SUCCESS);
		}
		deleted = 1;
	} while (e->index < NEXTINDEX(h) && __bt_cmp(t, key, e) == 0);

	/* Check for right-hand edge of the page. */
	if (e->index == NEXTINDEX(h))
		redo = 1;

	/* Delete from the key to the beginning of the page. */
	while (e->index-- > 0) {
		if (__bt_cmp(t, key, e) != 0)
			break;
		if (__bt_dleaf(t, key, h, e->index) == RET_ERROR) {
			mpool_put(t->bt_mp, h, 0);
			return (RET_ERROR);
		}
		if (e->index == 0)
			redo = 1;
	}

	/* Check for an empty page. */
	if (NEXTINDEX(h) == 0) {
		if (__bt_pdelete(t, h))
			return (RET_ERROR);
		goto loop;
	}

	/* Put the page. */
	mpool_put(t->bt_mp, h, MPOOL_DIRTY);

	if (redo)
		goto loop;
	return (RET_SUCCESS);
}

/*
 * __bt_pdelete --
 *	Delete a single page from the tree.
 *
 * Parameters:
 *	t:	tree
 *	h:	leaf page
 *
 * Returns:
 *	RET_SUCCESS, RET_ERROR.
 *
 * Side-effects:
 *	mpool_put's the page
 */
static int
__bt_pdelete(BTREE *t, PAGE *h)
{
	BINTERNAL *bi;
	PAGE *pg;
	EPGNO *parent;
	indx_t cnt, idx, *ip, offset;
	u_int32_t nksize;
	char *from;

	/*
	 * Walk the parent page stack -- a LIFO stack of the pages that were
	 * traversed when we searched for the page where the delete occurred.
	 * Each stack entry is a page number and a page index offset.  The
	 * offset is for the page traversed on the search.  We've just deleted
	 * a page, so we have to delete the key from the parent page.
	 *
	 * If the delete from the parent page makes it empty, this process may
	 * continue all the way up the tree.  We stop if we reach the root page
	 * (which is never deleted, it's just not worth the effort) or if the
	 * delete does not empty the page.
	 */
	while ((parent = BT_POP(t)) != NULL) {
		/* Get the parent page. */
		if ((pg = mpool_get(t->bt_mp, parent->pgno, 0)) == NULL)
			return (RET_ERROR);

		idx = parent->index;
		bi = GETBINTERNAL(pg, idx);

		/* Free any overflow pages. */
		if (bi->flags & P_BIGKEY &&
		    __ovfl_delete(t, bi->bytes) == RET_ERROR) {
			mpool_put(t->bt_mp, pg, 0);
			return (RET_ERROR);
		}

		/*
		 * Free the parent if it has only the one key and it's not the
		 * root page. If it's the rootpage, turn it back into an empty
		 * leaf page.
		 */
		if (NEXTINDEX(pg) == 1) {
			if (pg->pgno == P_ROOT) {
				pg->lower = BTDATAOFF;
				pg->upper = t->bt_psize;
				pg->flags = P_BLEAF;
			} else {
				if (__bt_relink(t, pg) || __bt_free(t, pg))
					return (RET_ERROR);
				continue;
			}
		} else {
			/* Pack remaining key items at the end of the page. */
			nksize = NBINTERNAL(bi->ksize);
			from = (char *)pg + pg->upper;
			memmove(from + nksize, from, (char *)bi - from);
			pg->upper += nksize;

			/* Adjust indices' offsets, shift the indices down. */
			offset = pg->linp[idx];
			for (cnt = idx, ip = &pg->linp[0]; cnt--; ++ip)
				if (ip[0] < offset)
					ip[0] += nksize;
			for (cnt = NEXTINDEX(pg) - idx; --cnt; ++ip)
				ip[0] = ip[1] < offset ? ip[1] + nksize : ip[1];
			pg->lower -= sizeof(indx_t);
		}

		mpool_put(t->bt_mp, pg, MPOOL_DIRTY);
		break;
	}

	/* Free the leaf page, as long as it wasn't the root. */
	if (h->pgno == P_ROOT) {
		mpool_put(t->bt_mp, h, MPOOL_DIRTY);
		return (RET_SUCCESS);
	}
	return (__bt_relink(t, h) || __bt_free(t, h));
}

/*
 * __bt_dleaf --
 *	Delete a single record from a leaf page.
 *
 * Parameters:
 *	t:	tree
 *    key:	referenced key
 *	h:	page
 *	idx:	index on page to delete
 *
 * Returns:
 *	RET_SUCCESS, RET_ERROR.
 */
int
__bt_dleaf(BTREE *t, const DBT *key, PAGE *h, u_int idx)
{
	BLEAF *bl;
	indx_t cnt, *ip, offset;
	u_int32_t nbytes;
	void *to;
	char *from;

	/* If this record is referenced by the cursor, delete the cursor. */
	if (F_ISSET(&t->bt_cursor, CURS_INIT) &&
	    !F_ISSET(&t->bt_cursor, CURS_ACQUIRE) &&
	    t->bt_cursor.pg.pgno == h->pgno && t->bt_cursor.pg.index == idx &&
	    __bt_curdel(t, key, h, idx))
		return (RET_ERROR);

	/* If the entry uses overflow pages, make them available for reuse. */
	to = bl = GETBLEAF(h, idx);
	if (bl->flags & P_BIGKEY && __ovfl_delete(t, bl->bytes) == RET_ERROR)
		return (RET_ERROR);
	if (bl->flags & P_BIGDATA &&
	    __ovfl_delete(t, bl->bytes + bl->ksize) == RET_ERROR)
		return (RET_ERROR);

	/* Pack the remaining key/data items at the end of the page. */
	nbytes = NBLEAF(bl);
	from = (char *)h + h->upper;
	memmove(from + nbytes, from, (char *)to - from);
	h->upper += nbytes;

	/* Adjust the indices' offsets, shift the indices down. */
	offset = h->linp[idx];
	for (cnt = idx, ip = &h->linp[0]; cnt--; ++ip)
		if (ip[0] < offset)
			ip[0] += nbytes;
	for (cnt = NEXTINDEX(h) - idx; --cnt; ++ip)
		ip[0] = ip[1] < offset ? ip[1] + nbytes : ip[1];
	h->lower -= sizeof(indx_t);

	/* If the cursor is on this page, adjust it as necessary. */
	if (F_ISSET(&t->bt_cursor, CURS_INIT) &&
	    !F_ISSET(&t->bt_cursor, CURS_ACQUIRE) &&
	    t->bt_cursor.pg.pgno == h->pgno && t->bt_cursor.pg.index > idx)
		--t->bt_cursor.pg.index;

	return (RET_SUCCESS);
}

/*
 * __bt_curdel --
 *	Delete the cursor.
 *
 * Parameters:
 *	t:	tree
 *    key:	referenced key (or NULL)
 *	h:	page
 *    idx:	index on page to delete
 *
 * Returns:
 *	RET_SUCCESS, RET_ERROR.
 */
static int
__bt_curdel(BTREE *t, const DBT *key, PAGE *h, u_int idx)
{
	CURSOR *c;
	EPG e;
	PAGE *pg;
	int curcopy, status;

	/*
	 * If there are duplicates, move forward or backward to one.
	 * Otherwise, copy the key into the cursor area.
	 */
	c = &t->bt_cursor;
	F_CLR(c, CURS_AFTER | CURS_BEFORE | CURS_ACQUIRE);

	curcopy = 0;
	if (!F_ISSET(t, B_NODUPS)) {
		/*
		 * We're going to have to do comparisons.  If we weren't
		 * provided a copy of the key, i.e. the user is deleting
		 * the current cursor position, get one.
		 */
		if (key == NULL) {
			e.page = h;
			e.index = idx;
			if ((status = __bt_ret(t, &e,
			    &c->key, &c->key, NULL, NULL, 1)) != RET_SUCCESS)
				return (status);
			curcopy = 1;
			key = &c->key;
		}
		/* Check previous key, if not at the beginning of the page. */
		if (idx > 0) {
			e.page = h;
			e.index = idx - 1;
			if (__bt_cmp(t, key, &e) == 0) {
				F_SET(c, CURS_BEFORE);
				goto dup2;
			}
		}
		/* Check next key, if not at the end of the page. */
		if (idx < NEXTINDEX(h) - 1) {
			e.page = h;
			e.index = idx + 1;
			if (__bt_cmp(t, key, &e) == 0) {
				F_SET(c, CURS_AFTER);
				goto dup2;
			}
		}
		/* Check previous key if at the beginning of the page. */
		if (idx == 0 && h->prevpg != P_INVALID) {
			if ((pg = mpool_get(t->bt_mp, h->prevpg, 0)) == NULL)
				return (RET_ERROR);
			e.page = pg;
			e.index = NEXTINDEX(pg) - 1;
			if (__bt_cmp(t, key, &e) == 0) {
				F_SET(c, CURS_BEFORE);
				goto dup1;
			}
			mpool_put(t->bt_mp, pg, 0);
		}
		/* Check next key if at the end of the page. */
		if (idx == NEXTINDEX(h) - 1 && h->nextpg != P_INVALID) {
			if ((pg = mpool_get(t->bt_mp, h->nextpg, 0)) == NULL)
				return (RET_ERROR);
			e.page = pg;
			e.index = 0;
			if (__bt_cmp(t, key, &e) == 0) {
				F_SET(c, CURS_AFTER);
dup1:				mpool_put(t->bt_mp, pg, 0);
dup2:				c->pg.pgno = e.page->pgno;
				c->pg.index = e.index;
				return (RET_SUCCESS);
			}
			mpool_put(t->bt_mp, pg, 0);
		}
	}
	e.page = h;
	e.index = idx;
	if (curcopy || (status =
	    __bt_ret(t, &e, &c->key, &c->key, NULL, NULL, 1)) == RET_SUCCESS) {
		F_SET(c, CURS_ACQUIRE);
		return (RET_SUCCESS);
	}
	return (status);
}

/*
 * __bt_relink --
 *	Link around a deleted page.
 *
 * Parameters:
 *	t:	tree
 *	h:	page to be deleted
 */
static int
__bt_relink(BTREE *t, PAGE *h)
{
	PAGE *pg;

	if (h->nextpg != P_INVALID) {
		if ((pg = mpool_get(t->bt_mp, h->nextpg, 0)) == NULL)
			return (RET_ERROR);
		pg->prevpg = h->prevpg;
		mpool_put(t->bt_mp, pg, MPOOL_DIRTY);
	}
	if (h->prevpg != P_INVALID) {
		if ((pg = mpool_get(t->bt_mp, h->prevpg, 0)) == NULL)
			return (RET_ERROR);
		pg->nextpg = h->nextpg;
		mpool_put(t->bt_mp, pg, MPOOL_DIRTY);
	}
	return (0);
}
