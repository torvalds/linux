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
static char sccsid[] = "@(#)rec_utils.c	8.6 (Berkeley) 7/16/94";
#endif /* LIBC_SCCS and not lint */
#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <db.h>
#include "recno.h"

/*
 * __rec_ret --
 *	Build return data.
 *
 * Parameters:
 *	t:	tree
 *	e:	key/data pair to be returned
 *   nrec:	record number
 *    key:	user's key structure
 *   data:	user's data structure
 *
 * Returns:
 *	RET_SUCCESS, RET_ERROR.
 */
int
__rec_ret(BTREE *t, EPG *e, recno_t nrec, DBT *key, DBT *data)
{
	RLEAF *rl;
	void *p;

	if (key == NULL)
		goto dataonly;

	/* We have to copy the key, it's not on the page. */
	if (sizeof(recno_t) > t->bt_rkey.size) {
		p = realloc(t->bt_rkey.data, sizeof(recno_t));
		if (p == NULL)
			return (RET_ERROR);
		t->bt_rkey.data = p;
		t->bt_rkey.size = sizeof(recno_t);
	}
	memmove(t->bt_rkey.data, &nrec, sizeof(recno_t));
	key->size = sizeof(recno_t);
	key->data = t->bt_rkey.data;

dataonly:
	if (data == NULL)
		return (RET_SUCCESS);

	/*
	 * We must copy big keys/data to make them contigous.  Otherwise,
	 * leave the page pinned and don't copy unless the user specified
	 * concurrent access.
	 */
	rl = GETRLEAF(e->page, e->index);
	if (rl->flags & P_BIGDATA) {
		if (__ovfl_get(t, rl->bytes,
		    &data->size, &t->bt_rdata.data, &t->bt_rdata.size))
			return (RET_ERROR);
		data->data = t->bt_rdata.data;
	} else if (F_ISSET(t, B_DB_LOCK)) {
		/* Use +1 in case the first record retrieved is 0 length. */
		if (rl->dsize + 1 > t->bt_rdata.size) {
			p = realloc(t->bt_rdata.data, rl->dsize + 1);
			if (p == NULL)
				return (RET_ERROR);
			t->bt_rdata.data = p;
			t->bt_rdata.size = rl->dsize + 1;
		}
		memmove(t->bt_rdata.data, rl->bytes, rl->dsize);
		data->size = rl->dsize;
		data->data = t->bt_rdata.data;
	} else {
		data->size = rl->dsize;
		data->data = rl->bytes;
	}
	return (RET_SUCCESS);
}
