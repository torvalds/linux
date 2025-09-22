/*	$OpenBSD: rec_delete.c,v 1.10 2005/08/05 13:03:00 espie Exp $	*/

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

#include <errno.h>
#include <stdio.h>
#include <string.h>

#include <db.h>
#include "recno.h"

static int rec_rdelete(BTREE *, recno_t);

/*
 * __REC_DELETE -- Delete the item(s) referenced by a key.
 *
 * Parameters:
 *	dbp:	pointer to access method
 *	key:	key to delete
 *	flags:	R_CURSOR if deleting what the cursor references
 *
 * Returns:
 *	RET_ERROR, RET_SUCCESS and RET_SPECIAL if the key not found.
 */
int
__rec_delete(const DB *dbp, const DBT *key, u_int flags)
{
	BTREE *t;
	recno_t nrec;
	int status;

	t = dbp->internal;

	/* Toss any page pinned across calls. */
	if (t->bt_pinned != NULL) {
		mpool_put(t->bt_mp, t->bt_pinned, 0);
		t->bt_pinned = NULL;
	}

	switch(flags) {
	case 0:
		if ((nrec = *(recno_t *)key->data) == 0)
			goto einval;
		if (nrec > t->bt_nrecs)
			return (RET_SPECIAL);
		--nrec;
		status = rec_rdelete(t, nrec);
		break;
	case R_CURSOR:
		if (!F_ISSET(&t->bt_cursor, CURS_INIT))
			goto einval;
		if (t->bt_nrecs == 0)
			return (RET_SPECIAL);
		status = rec_rdelete(t, t->bt_cursor.rcursor - 1);
		if (status == RET_SUCCESS)
			--t->bt_cursor.rcursor;
		break;
	default:
einval:		errno = EINVAL;
		return (RET_ERROR);
	}

	if (status == RET_SUCCESS)
		F_SET(t, B_MODIFIED | R_MODIFIED);
	return (status);
}

/*
 * REC_RDELETE -- Delete the data matching the specified key.
 *
 * Parameters:
 *	tree:	tree
 *	nrec:	record to delete
 *
 * Returns:
 *	RET_ERROR, RET_SUCCESS and RET_SPECIAL if the key not found.
 */
static int
rec_rdelete(BTREE *t, recno_t nrec)
{
	EPG *e;
	PAGE *h;
	int status;

	/* Find the record; __rec_search pins the page. */
	if ((e = __rec_search(t, nrec, SDELETE)) == NULL)
		return (RET_ERROR);

	/* Delete the record. */
	h = e->page;
	status = __rec_dleaf(t, h, e->index);
	if (status != RET_SUCCESS) {
		mpool_put(t->bt_mp, h, 0);
		return (status);
	}
	mpool_put(t->bt_mp, h, MPOOL_DIRTY);
	return (RET_SUCCESS);
}

/*
 * __REC_DLEAF -- Delete a single record from a recno leaf page.
 *
 * Parameters:
 *	t:	tree
 *	idx:	index on current page to delete
 *
 * Returns:
 *	RET_SUCCESS, RET_ERROR.
 */
int
__rec_dleaf(BTREE *t, PAGE *h, u_int32_t idx)
{
	RLEAF *rl;
	indx_t *ip, cnt, offset;
	u_int32_t nbytes;
	char *from;
	void *to;

	/*
	 * Delete a record from a recno leaf page.  Internal records are never
	 * deleted from internal pages, regardless of the records that caused
	 * them to be added being deleted.  Pages made empty by deletion are
	 * not reclaimed.  They are, however, made available for reuse.
	 *
	 * Pack the remaining entries at the end of the page, shift the indices
	 * down, overwriting the deleted record and its index.  If the record
	 * uses overflow pages, make them available for reuse.
	 */
	to = rl = GETRLEAF(h, idx);
	if (rl->flags & P_BIGDATA && __ovfl_delete(t, rl->bytes) == RET_ERROR)
		return (RET_ERROR);
	nbytes = NRLEAF(rl);

	/*
	 * Compress the key/data pairs.  Compress and adjust the [BR]LEAF
	 * offsets.  Reset the headers.
	 */
	from = (char *)h + h->upper;
	memmove(from + nbytes, from, (char *)to - from);
	h->upper += nbytes;

	offset = h->linp[idx];
	for (cnt = &h->linp[idx] - (ip = &h->linp[0]); cnt--; ++ip)
		if (ip[0] < offset)
			ip[0] += nbytes;
	for (cnt = &h->linp[NEXTINDEX(h)] - ip; --cnt; ++ip)
		ip[0] = ip[1] < offset ? ip[1] + nbytes : ip[1];
	h->lower -= sizeof(indx_t);
	--t->bt_nrecs;
	return (RET_SUCCESS);
}
