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
static char sccsid[] = "@(#)bt_utils.c	8.8 (Berkeley) 7/20/94";
#endif /* LIBC_SCCS and not lint */
#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <db.h>
#include "btree.h"

/*
 * __bt_ret --
 *	Build return key/data pair.
 *
 * Parameters:
 *	t:	tree
 *	e:	key/data pair to be returned
 *	key:	user's key structure (NULL if not to be filled in)
 *	rkey:	memory area to hold key
 *	data:	user's data structure (NULL if not to be filled in)
 *	rdata:	memory area to hold data
 *       copy:	always copy the key/data item
 *
 * Returns:
 *	RET_SUCCESS, RET_ERROR.
 */
int
__bt_ret(BTREE *t, EPG *e, DBT *key, DBT *rkey, DBT *data, DBT *rdata, int copy)
{
	BLEAF *bl;
	void *p;

	bl = GETBLEAF(e->page, e->index);

	/*
	 * We must copy big keys/data to make them contigous.  Otherwise,
	 * leave the page pinned and don't copy unless the user specified
	 * concurrent access.
	 */
	if (key == NULL)
		goto dataonly;

	if (bl->flags & P_BIGKEY) {
		if (__ovfl_get(t, bl->bytes,
		    &key->size, &rkey->data, &rkey->size))
			return (RET_ERROR);
		key->data = rkey->data;
	} else if (copy || F_ISSET(t, B_DB_LOCK)) {
		if (bl->ksize > rkey->size) {
			p = realloc(rkey->data, bl->ksize);
			if (p == NULL)
				return (RET_ERROR);
			rkey->data = p;
			rkey->size = bl->ksize;
		}
		memmove(rkey->data, bl->bytes, bl->ksize);
		key->size = bl->ksize;
		key->data = rkey->data;
	} else {
		key->size = bl->ksize;
		key->data = bl->bytes;
	}

dataonly:
	if (data == NULL)
		return (RET_SUCCESS);

	if (bl->flags & P_BIGDATA) {
		if (__ovfl_get(t, bl->bytes + bl->ksize,
		    &data->size, &rdata->data, &rdata->size))
			return (RET_ERROR);
		data->data = rdata->data;
	} else if (copy || F_ISSET(t, B_DB_LOCK)) {
		/* Use +1 in case the first record retrieved is 0 length. */
		if (bl->dsize + 1 > rdata->size) {
			p = realloc(rdata->data, bl->dsize + 1);
			if (p == NULL)
				return (RET_ERROR);
			rdata->data = p;
			rdata->size = bl->dsize + 1;
		}
		memmove(rdata->data, bl->bytes + bl->ksize, bl->dsize);
		data->size = bl->dsize;
		data->data = rdata->data;
	} else {
		data->size = bl->dsize;
		data->data = bl->bytes + bl->ksize;
	}

	return (RET_SUCCESS);
}

/*
 * __BT_CMP -- Compare a key to a given record.
 *
 * Parameters:
 *	t:	tree
 *	k1:	DBT pointer of first arg to comparison
 *	e:	pointer to EPG for comparison
 *
 * Returns:
 *	< 0 if k1 is < record
 *	= 0 if k1 is = record
 *	> 0 if k1 is > record
 */
int
__bt_cmp(BTREE *t, const DBT *k1, EPG *e)
{
	BINTERNAL *bi;
	BLEAF *bl;
	DBT k2;
	PAGE *h;
	void *bigkey;

	/*
	 * The left-most key on internal pages, at any level of the tree, is
	 * guaranteed by the following code to be less than any user key.
	 * This saves us from having to update the leftmost key on an internal
	 * page when the user inserts a new key in the tree smaller than
	 * anything we've yet seen.
	 */
	h = e->page;
	if (e->index == 0 && h->prevpg == P_INVALID && !(h->flags & P_BLEAF))
		return (1);

	bigkey = NULL;
	if (h->flags & P_BLEAF) {
		bl = GETBLEAF(h, e->index);
		if (bl->flags & P_BIGKEY)
			bigkey = bl->bytes;
		else {
			k2.data = bl->bytes;
			k2.size = bl->ksize;
		}
	} else {
		bi = GETBINTERNAL(h, e->index);
		if (bi->flags & P_BIGKEY)
			bigkey = bi->bytes;
		else {
			k2.data = bi->bytes;
			k2.size = bi->ksize;
		}
	}

	if (bigkey) {
		if (__ovfl_get(t, bigkey,
		    &k2.size, &t->bt_rdata.data, &t->bt_rdata.size))
			return (RET_ERROR);
		k2.data = t->bt_rdata.data;
	}
	return ((*t->bt_cmp)(k1, &k2));
}

/*
 * __BT_DEFCMP -- Default comparison routine.
 *
 * Parameters:
 *	a:	DBT #1
 *	b:	DBT #2
 *
 * Returns:
 *	< 0 if a is < b
 *	= 0 if a is = b
 *	> 0 if a is > b
 */
int
__bt_defcmp(const DBT *a, const DBT *b)
{
	size_t len;
	u_char *p1, *p2;

	/*
	 * XXX
	 * If a size_t doesn't fit in an int, this routine can lose.
	 * What we need is an integral type which is guaranteed to be
	 * larger than a size_t, and there is no such thing.
	 */
	len = MIN(a->size, b->size);
	for (p1 = a->data, p2 = b->data; len--; ++p1, ++p2)
		if (*p1 != *p2)
			return ((int)*p1 - (int)*p2);
	return ((int)a->size - (int)b->size);
}

/*
 * __BT_DEFPFX -- Default prefix routine.
 *
 * Parameters:
 *	a:	DBT #1
 *	b:	DBT #2
 *
 * Returns:
 *	Number of bytes needed to distinguish b from a.
 */
size_t
__bt_defpfx(const DBT *a, const DBT *b)
{
	u_char *p1, *p2;
	size_t cnt, len;

	cnt = 1;
	len = MIN(a->size, b->size);
	for (p1 = a->data, p2 = b->data; len--; ++p1, ++p2, ++cnt)
		if (*p1 != *p2)
			return (cnt);

	/* a->size must be <= b->size, or they wouldn't be in this order. */
	return (a->size < b->size ? a->size + 1 : a->size);
}
