/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 1983, 1993
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

#include <sys/cdefs.h>
__SCCSID("@(#)telldir.c	8.1 (Berkeley) 6/4/93");
__FBSDID("$FreeBSD$");

#include "namespace.h"
#include <sys/param.h>
#include <sys/queue.h>
#include <dirent.h>
#include <pthread.h>
#include <stdlib.h>
#include <unistd.h>
#include "un-namespace.h"

#include "libc_private.h"
#include "gen-private.h"
#include "telldir.h"

/*
 * return a pointer into a directory
 */
long
telldir(DIR *dirp)
{
	struct ddloc_mem *lp, *flp;
	union ddloc_packed ddloc;

	if (__isthreaded)
		_pthread_mutex_lock(&dirp->dd_lock);
	/* 
	 * Outline:
	 * 1) If the directory position fits in a packed structure, return that.
	 * 2) Otherwise, see if it's already been recorded in the linked list
	 * 3) Otherwise, malloc a new one
	 */
	if (dirp->dd_seek < (1ul << DD_SEEK_BITS) &&
	    dirp->dd_loc < (1ul << DD_LOC_BITS)) {
		ddloc.s.is_packed = 1;
		ddloc.s.loc = dirp->dd_loc;
		ddloc.s.seek = dirp->dd_seek;
		goto out;
	}

	flp = NULL;
	LIST_FOREACH(lp, &dirp->dd_td->td_locq, loc_lqe) {
		if (lp->loc_seek == dirp->dd_seek) {
			if (flp == NULL)
				flp = lp;
			if (lp->loc_loc == dirp->dd_loc)
				break;
		} else if (flp != NULL) {
			lp = NULL;
			break;
		}
	}
	if (lp == NULL) {
		lp = malloc(sizeof(struct ddloc_mem));
		if (lp == NULL) {
			if (__isthreaded)
				_pthread_mutex_unlock(&dirp->dd_lock);
			return (-1);
		}
		lp->loc_index = dirp->dd_td->td_loccnt++;
		lp->loc_seek = dirp->dd_seek;
		lp->loc_loc = dirp->dd_loc;
		if (flp != NULL)
			LIST_INSERT_BEFORE(flp, lp, loc_lqe);
		else
			LIST_INSERT_HEAD(&dirp->dd_td->td_locq, lp, loc_lqe);
	}
	ddloc.i.is_packed = 0;
	/* 
	 * Technically this assignment could overflow on 32-bit architectures,
	 * but we would get ENOMEM long before that happens.
	 */
	ddloc.i.index = lp->loc_index;

out:
	if (__isthreaded)
		_pthread_mutex_unlock(&dirp->dd_lock);
	return (ddloc.l);
}

/*
 * seek to an entry in a directory.
 * Only values returned by "telldir" should be passed to seekdir.
 */
void
_seekdir(DIR *dirp, long loc)
{
	struct ddloc_mem *lp;
	struct dirent *dp;
	union ddloc_packed ddloc;
	off_t loc_seek;
	long loc_loc;

	ddloc.l = loc;

	if (ddloc.s.is_packed) {
		loc_seek = ddloc.s.seek;
		loc_loc = ddloc.s.loc;
	} else {
		LIST_FOREACH(lp, &dirp->dd_td->td_locq, loc_lqe) {
			if (lp->loc_index == ddloc.i.index)
				break;
		}
		if (lp == NULL)
			return;

		loc_seek = lp->loc_seek;
		loc_loc = lp->loc_loc;
	}
	if (loc_loc == dirp->dd_loc && loc_seek == dirp->dd_seek)
		return;

	/* If it's within the same chunk of data, don't bother reloading. */
	if (loc_seek == dirp->dd_seek) {
		/*
		 * If we go back to 0 don't make the next readdir
		 * trigger a call to getdirentries().
		 */
		if (loc_loc == 0)
			dirp->dd_flags |= __DTF_SKIPREAD;
		dirp->dd_loc = loc_loc;
		return;
	}
	(void) lseek(dirp->dd_fd, (off_t)loc_seek, SEEK_SET);
	dirp->dd_seek = loc_seek;
	dirp->dd_loc = 0;
	dirp->dd_flags &= ~__DTF_SKIPREAD; /* current contents are invalid */
	while (dirp->dd_loc < loc_loc) {
		dp = _readdir_unlocked(dirp, 0);
		if (dp == NULL)
			break;
	}
}

/*
 * After readdir returns the last entry in a block, a call to telldir
 * returns a location that is after the end of that last entry.
 * However, that location doesn't refer to a valid directory entry.
 * Ideally, the call to telldir would return a location that refers to
 * the first entry in the next block.  That location is not known
 * until the next block is read, so readdir calls this function after
 * fetching a new block to fix any such telldir locations.
 */
void
_fixtelldir(DIR *dirp, long oldseek, long oldloc)
{
	struct ddloc_mem *lp;

	lp = LIST_FIRST(&dirp->dd_td->td_locq);
	if (lp != NULL) {
		if (lp->loc_loc == oldloc &&
		    lp->loc_seek == oldseek) {
			lp->loc_seek = dirp->dd_seek;
			lp->loc_loc = dirp->dd_loc;
		}
	}
}

/*
 * Reclaim memory for telldir cookies which weren't used.
 */
void
_reclaim_telldir(DIR *dirp)
{
	struct ddloc_mem *lp;
	struct ddloc_mem *templp;

	lp = LIST_FIRST(&dirp->dd_td->td_locq);
	while (lp != NULL) {
		templp = lp;
		lp = LIST_NEXT(lp, loc_lqe);
		free(templp);
	}
	LIST_INIT(&dirp->dd_td->td_locq);
}
