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
static char sccsid[] = "@(#)bt_search.c	8.8 (Berkeley) 7/31/94";
#endif /* LIBC_SCCS and not lint */
#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/types.h>

#include <stdio.h>

#include <db.h>
#include "btree.h"

static int __bt_snext(BTREE *, PAGE *, const DBT *, int *);
static int __bt_sprev(BTREE *, PAGE *, const DBT *, int *);

/*
 * __bt_search --
 *	Search a btree for a key.
 *
 * Parameters:
 *	t:	tree to search
 *	key:	key to find
 *	exactp:	pointer to exact match flag
 *
 * Returns:
 *	The EPG for matching record, if any, or the EPG for the location
 *	of the key, if it were inserted into the tree, is entered into
 *	the bt_cur field of the tree.  A pointer to the field is returned.
 */
EPG *
__bt_search(BTREE *t, const DBT *key, int *exactp)
{
	PAGE *h;
	indx_t base, idx, lim;
	pgno_t pg;
	int cmp;

	BT_CLR(t);
	for (pg = P_ROOT;;) {
		if ((h = mpool_get(t->bt_mp, pg, 0)) == NULL)
			return (NULL);

		/* Do a binary search on the current page. */
		t->bt_cur.page = h;
		for (base = 0, lim = NEXTINDEX(h); lim; lim >>= 1) {
			t->bt_cur.index = idx = base + (lim >> 1);
			if ((cmp = __bt_cmp(t, key, &t->bt_cur)) == 0) {
				if (h->flags & P_BLEAF) {
					*exactp = 1;
					return (&t->bt_cur);
				}
				goto next;
			}
			if (cmp > 0) {
				base = idx + 1;
				--lim;
			}
		}

		/*
		 * If it's a leaf page, we're almost done.  If no duplicates
		 * are allowed, or we have an exact match, we're done.  Else,
		 * it's possible that there were matching keys on this page,
		 * which later deleted, and we're on a page with no matches
		 * while there are matches on other pages.  If at the start or
		 * end of a page, check the adjacent page.
		 */
		if (h->flags & P_BLEAF) {
			if (!F_ISSET(t, B_NODUPS)) {
				if (base == 0 &&
				    h->prevpg != P_INVALID &&
				    __bt_sprev(t, h, key, exactp))
					return (&t->bt_cur);
				if (base == NEXTINDEX(h) &&
				    h->nextpg != P_INVALID &&
				    __bt_snext(t, h, key, exactp))
					return (&t->bt_cur);
			}
			*exactp = 0;
			t->bt_cur.index = base;
			return (&t->bt_cur);
		}

		/*
		 * No match found.  Base is the smallest index greater than
		 * key and may be zero or a last + 1 index.  If it's non-zero,
		 * decrement by one, and record the internal page which should
		 * be a parent page for the key.  If a split later occurs, the
		 * inserted page will be to the right of the saved page.
		 */
		idx = base ? base - 1 : base;

next:		BT_PUSH(t, h->pgno, idx);
		pg = GETBINTERNAL(h, idx)->pgno;
		mpool_put(t->bt_mp, h, 0);
	}
}

/*
 * __bt_snext --
 *	Check for an exact match after the key.
 *
 * Parameters:
 *	t:	tree
 *	h:	current page
 *	key:	key
 *	exactp:	pointer to exact match flag
 *
 * Returns:
 *	If an exact match found.
 */
static int
__bt_snext(BTREE *t, PAGE *h, const DBT *key, int *exactp)
{
	EPG e;

	/*
	 * Get the next page.  The key is either an exact
	 * match, or not as good as the one we already have.
	 */
	if ((e.page = mpool_get(t->bt_mp, h->nextpg, 0)) == NULL)
		return (0);
	e.index = 0;
	if (__bt_cmp(t, key, &e) == 0) {
		mpool_put(t->bt_mp, h, 0);
		t->bt_cur = e;
		*exactp = 1;
		return (1);
	}
	mpool_put(t->bt_mp, e.page, 0);
	return (0);
}

/*
 * __bt_sprev --
 *	Check for an exact match before the key.
 *
 * Parameters:
 *	t:	tree
 *	h:	current page
 *	key:	key
 *	exactp:	pointer to exact match flag
 *
 * Returns:
 *	If an exact match found.
 */
static int
__bt_sprev(BTREE *t, PAGE *h, const DBT *key, int *exactp)
{
	EPG e;

	/*
	 * Get the previous page.  The key is either an exact
	 * match, or not as good as the one we already have.
	 */
	if ((e.page = mpool_get(t->bt_mp, h->prevpg, 0)) == NULL)
		return (0);
	e.index = NEXTINDEX(e.page) - 1;
	if (__bt_cmp(t, key, &e) == 0) {
		mpool_put(t->bt_mp, h, 0);
		t->bt_cur = e;
		*exactp = 1;
		return (1);
	}
	mpool_put(t->bt_mp, e.page, 0);
	return (0);
}
