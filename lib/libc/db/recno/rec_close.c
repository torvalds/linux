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
static char sccsid[] = "@(#)rec_close.c	8.6 (Berkeley) 8/18/94";
#endif /* LIBC_SCCS and not lint */
#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include "namespace.h"
#include <sys/types.h>
#include <sys/uio.h>
#include <sys/mman.h>

#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <unistd.h>
#include "un-namespace.h"

#include <db.h>
#include "recno.h"

/*
 * __REC_CLOSE -- Close a recno tree.
 *
 * Parameters:
 *	dbp:	pointer to access method
 *
 * Returns:
 *	RET_ERROR, RET_SUCCESS
 */
int
__rec_close(DB *dbp)
{
	BTREE *t;
	int status;

	t = dbp->internal;

	/* Toss any page pinned across calls. */
	if (t->bt_pinned != NULL) {
		mpool_put(t->bt_mp, t->bt_pinned, 0);
		t->bt_pinned = NULL;
	}

	if (__rec_sync(dbp, 0) == RET_ERROR)
		return (RET_ERROR);

	/* Committed to closing. */
	status = RET_SUCCESS;
	if (F_ISSET(t, R_MEMMAPPED) && munmap(t->bt_smap, t->bt_msize))
		status = RET_ERROR;

	if (!F_ISSET(t, R_INMEM)) {
		if (F_ISSET(t, R_CLOSEFP)) {
			if (fclose(t->bt_rfp))
				status = RET_ERROR;
		} else {
			if (_close(t->bt_rfd))
				status = RET_ERROR;
		}
	}

	if (__bt_close(dbp) == RET_ERROR)
		status = RET_ERROR;

	return (status);
}

/*
 * __REC_SYNC -- sync the recno tree to disk.
 *
 * Parameters:
 *	dbp:	pointer to access method
 *
 * Returns:
 *	RET_SUCCESS, RET_ERROR.
 */
int
__rec_sync(const DB *dbp, u_int flags)
{
	struct iovec iov[2];
	BTREE *t;
	DBT data, key;
	off_t off;
	recno_t scursor, trec;
	int status;

	t = dbp->internal;

	/* Toss any page pinned across calls. */
	if (t->bt_pinned != NULL) {
		mpool_put(t->bt_mp, t->bt_pinned, 0);
		t->bt_pinned = NULL;
	}

	if (flags == R_RECNOSYNC)
		return (__bt_sync(dbp, 0));

	if (F_ISSET(t, R_RDONLY | R_INMEM) || !F_ISSET(t, R_MODIFIED))
		return (RET_SUCCESS);

	/* Read any remaining records into the tree. */
	if (!F_ISSET(t, R_EOF) && t->bt_irec(t, MAX_REC_NUMBER) == RET_ERROR)
		return (RET_ERROR);

	/* Rewind the file descriptor. */
	if (lseek(t->bt_rfd, (off_t)0, SEEK_SET) != 0)
		return (RET_ERROR);

	/* Save the cursor. */
	scursor = t->bt_cursor.rcursor;

	key.size = sizeof(recno_t);
	key.data = &trec;

	if (F_ISSET(t, R_FIXLEN)) {
		/*
		 * We assume that fixed length records are all fixed length.
		 * Any that aren't are either EINVAL'd or corrected by the
		 * record put code.
		 */
		status = (dbp->seq)(dbp, &key, &data, R_FIRST);
		while (status == RET_SUCCESS) {
			if (_write(t->bt_rfd, data.data, data.size) !=
			    (ssize_t)data.size)
				return (RET_ERROR);
			status = (dbp->seq)(dbp, &key, &data, R_NEXT);
		}
	} else {
		iov[1].iov_base = &t->bt_bval;
		iov[1].iov_len = 1;

		status = (dbp->seq)(dbp, &key, &data, R_FIRST);
		while (status == RET_SUCCESS) {
			iov[0].iov_base = data.data;
			iov[0].iov_len = data.size;
			if (_writev(t->bt_rfd, iov, 2) != (ssize_t)(data.size + 1))
				return (RET_ERROR);
			status = (dbp->seq)(dbp, &key, &data, R_NEXT);
		}
	}

	/* Restore the cursor. */
	t->bt_cursor.rcursor = scursor;

	if (status == RET_ERROR)
		return (RET_ERROR);
	if ((off = lseek(t->bt_rfd, (off_t)0, SEEK_CUR)) == -1)
		return (RET_ERROR);
	if (ftruncate(t->bt_rfd, off))
		return (RET_ERROR);
	F_CLR(t, R_MODIFIED);
	return (RET_SUCCESS);
}
