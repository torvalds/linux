/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 1990, 1993, 1994
 *	The Regents of the University of California.  All rights reserved.
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
static char sccsid[] = "@(#)rec_put.c	8.7 (Berkeley) 8/18/94";
#endif /* LIBC_SCCS and not lint */
#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/types.h>

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <db.h>
#include "recno.h"

/*
 * __REC_PUT -- Add a recno item to the tree.
 *
 * Parameters:
 *	dbp:	pointer to access method
 *	key:	key
 *	data:	data
 *	flag:	R_CURSOR, R_IAFTER, R_IBEFORE, R_NOOVERWRITE
 *
 * Returns:
 *	RET_ERROR, RET_SUCCESS and RET_SPECIAL if the key is
 *	already in the tree and R_NOOVERWRITE specified.
 */
int
__rec_put(const DB *dbp, DBT *key, const DBT *data, u_int flags)
{
	BTREE *t;
	DBT fdata, tdata;
	recno_t nrec;
	int status;

	t = dbp->internal;

	/* Toss any page pinned across calls. */
	if (t->bt_pinned != NULL) {
		mpool_put(t->bt_mp, t->bt_pinned, 0);
		t->bt_pinned = NULL;
	}

	/*
	 * If using fixed-length records, and the record is long, return
	 * EINVAL.  If it's short, pad it out.  Use the record data return
	 * memory, it's only short-term.
	 */
	if (F_ISSET(t, R_FIXLEN) && data->size != t->bt_reclen) {
		if (data->size > t->bt_reclen)
			goto einval;

		if (t->bt_rdata.size < t->bt_reclen) {
			t->bt_rdata.data = 
			    reallocf(t->bt_rdata.data, t->bt_reclen);
			if (t->bt_rdata.data == NULL)
				return (RET_ERROR);
			t->bt_rdata.size = t->bt_reclen;
		}
		memmove(t->bt_rdata.data, data->data, data->size);
		memset((char *)t->bt_rdata.data + data->size,
		    t->bt_bval, t->bt_reclen - data->size);
		fdata.data = t->bt_rdata.data;
		fdata.size = t->bt_reclen;
	} else {
		fdata.data = data->data;
		fdata.size = data->size;
	}

	switch (flags) {
	case R_CURSOR:
		if (!F_ISSET(&t->bt_cursor, CURS_INIT))
			goto einval;
		nrec = t->bt_cursor.rcursor;
		break;
	case R_SETCURSOR:
		if ((nrec = *(recno_t *)key->data) == 0)
			goto einval;
		break;
	case R_IAFTER:
		if ((nrec = *(recno_t *)key->data) == 0) {
			nrec = 1;
			flags = R_IBEFORE;
		}
		break;
	case 0:
	case R_IBEFORE:
		if ((nrec = *(recno_t *)key->data) == 0)
			goto einval;
		break;
	case R_NOOVERWRITE:
		if ((nrec = *(recno_t *)key->data) == 0)
			goto einval;
		if (nrec <= t->bt_nrecs)
			return (RET_SPECIAL);
		break;
	default:
einval:		errno = EINVAL;
		return (RET_ERROR);
	}

	/*
	 * Make sure that records up to and including the put record are
	 * already in the database.  If skipping records, create empty ones.
	 */
	if (nrec > t->bt_nrecs) {
		if (!F_ISSET(t, R_EOF | R_INMEM) &&
		    t->bt_irec(t, nrec) == RET_ERROR)
			return (RET_ERROR);
		if (nrec > t->bt_nrecs + 1) {
			if (F_ISSET(t, R_FIXLEN)) {
				if ((tdata.data = malloc(t->bt_reclen)) == NULL)
					return (RET_ERROR);
				tdata.size = t->bt_reclen;
				memset(tdata.data, t->bt_bval, tdata.size);
			} else {
				tdata.data = NULL;
				tdata.size = 0;
			}
			while (nrec > t->bt_nrecs + 1)
				if (__rec_iput(t,
				    t->bt_nrecs, &tdata, 0) != RET_SUCCESS)
					return (RET_ERROR);
			if (F_ISSET(t, R_FIXLEN))
				free(tdata.data);
		}
	}

	if ((status = __rec_iput(t, nrec - 1, &fdata, flags)) != RET_SUCCESS)
		return (status);

	switch (flags) {
	case R_IAFTER:
		nrec++;
		break;
	case R_SETCURSOR:
		t->bt_cursor.rcursor = nrec;
		break;
	}

	F_SET(t, R_MODIFIED);
	return (__rec_ret(t, NULL, nrec, key, NULL));
}

/*
 * __REC_IPUT -- Add a recno item to the tree.
 *
 * Parameters:
 *	t:	tree
 *	nrec:	record number
 *	data:	data
 *
 * Returns:
 *	RET_ERROR, RET_SUCCESS
 */
int
__rec_iput(BTREE *t, recno_t nrec, const DBT *data, u_int flags)
{
	DBT tdata;
	EPG *e;
	PAGE *h;
	indx_t idx, nxtindex;
	pgno_t pg;
	u_int32_t nbytes;
	int dflags, status;
	char *dest, db[NOVFLSIZE];

	/*
	 * If the data won't fit on a page, store it on indirect pages.
	 *
	 * XXX
	 * If the insert fails later on, these pages aren't recovered.
	 */
	if (data->size > t->bt_ovflsize) {
		if (__ovfl_put(t, data, &pg) == RET_ERROR)
			return (RET_ERROR);
		tdata.data = db;
		tdata.size = NOVFLSIZE;
		memcpy(db, &pg, sizeof(pg));
		*(u_int32_t *)(db + sizeof(pgno_t)) = data->size;
		dflags = P_BIGDATA;
		data = &tdata;
	} else
		dflags = 0;

	/* __rec_search pins the returned page. */
	if ((e = __rec_search(t, nrec,
	    nrec > t->bt_nrecs || flags == R_IAFTER || flags == R_IBEFORE ?
	    SINSERT : SEARCH)) == NULL)
		return (RET_ERROR);

	h = e->page;
	idx = e->index;

	/*
	 * Add the specified key/data pair to the tree.  The R_IAFTER and
	 * R_IBEFORE flags insert the key after/before the specified key.
	 *
	 * Pages are split as required.
	 */
	switch (flags) {
	case R_IAFTER:
		++idx;
		break;
	case R_IBEFORE:
		break;
	default:
		if (nrec < t->bt_nrecs &&
		    __rec_dleaf(t, h, idx) == RET_ERROR) {
			mpool_put(t->bt_mp, h, 0);
			return (RET_ERROR);
		}
		break;
	}

	/*
	 * If not enough room, split the page.  The split code will insert
	 * the key and data and unpin the current page.  If inserting into
	 * the offset array, shift the pointers up.
	 */
	nbytes = NRLEAFDBT(data->size);
	if ((u_int32_t)(h->upper - h->lower) < nbytes + sizeof(indx_t)) {
		status = __bt_split(t, h, NULL, data, dflags, nbytes, idx);
		if (status == RET_SUCCESS)
			++t->bt_nrecs;
		return (status);
	}

	if (idx < (nxtindex = NEXTINDEX(h)))
		memmove(h->linp + idx + 1, h->linp + idx,
		    (nxtindex - idx) * sizeof(indx_t));
	h->lower += sizeof(indx_t);

	h->linp[idx] = h->upper -= nbytes;
	dest = (char *)h + h->upper;
	WR_RLEAF(dest, data, dflags);

	++t->bt_nrecs;
	F_SET(t, B_MODIFIED);
	mpool_put(t->bt_mp, h, MPOOL_DIRTY);

	return (RET_SUCCESS);
}
