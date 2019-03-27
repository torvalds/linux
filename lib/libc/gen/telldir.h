/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 1983, 1993
 *	The Regents of the University of California.  All rights reserved.
 *
 * Copyright (c) 2000
 * 	Daniel Eischen.  All rights reserved.
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
 *
 * $FreeBSD$
 */

#ifndef _TELLDIR_H_
#define	_TELLDIR_H_

#include <sys/queue.h>
#include <stdbool.h>

/*
 * telldir will malloc one of these to describe the current directory position,
 * if it can't fit that information into the packed structure below.  It
 * records the current magic cookie returned by getdirentries and the offset
 * within the buffer associated with that return value.
 */
struct ddloc_mem {
	LIST_ENTRY(ddloc_mem) loc_lqe; /* entry in list */
	long	loc_index;	/* key associated with structure */
	off_t	loc_seek;	/* magic cookie returned by getdirentries */
	long	loc_loc;	/* offset of entry in buffer */
};

#ifdef __LP64__
#define DD_LOC_BITS	31
#define DD_SEEK_BITS	32
#define DD_INDEX_BITS	63
#else
#define DD_LOC_BITS	12
#define DD_SEEK_BITS	19
#define DD_INDEX_BITS	31
#endif

/*
 * This is the real type returned by telldir.  telldir will prefer to return
 * the packed type, if possible, or the malloced type otherwise.  For msdosfs,
 * UFS, and NFS, directory positions usually fit within the packed type.  For
 * ZFS and tmpfs, they usually fit within the packed type on 64-bit
 * architectures.
 */
union ddloc_packed {
	long	l;		/* Opaque type returned by telldir(3) */
	struct {
		/* Identifies union type.  Must be 0. */
		unsigned long is_packed:1;
		/* Index into directory's linked list of ddloc_mem */
		unsigned long index:DD_INDEX_BITS;
	} __packed i;
	struct {
		/* Identifies union type.  Must be 1. */
		unsigned long is_packed:1;
		/* offset of entry in buffer*/
		unsigned long loc:DD_LOC_BITS;
		/* magic cookie returned by getdirentries */
		unsigned long seek:DD_SEEK_BITS;
	} __packed s;
};

_Static_assert(sizeof(long) == sizeof(union ddloc_packed),
    "packed telldir size mismatch!");

/*
 * One of these structures is malloced for each DIR to record telldir
 * positions.
 */
struct _telldir {
	LIST_HEAD(, ddloc_mem) td_locq; /* list of locations */
	long	td_loccnt;	/* index of entry for sequential readdir's */
};

bool		_filldir(DIR *, bool);
struct dirent	*_readdir_unlocked(DIR *, int);
void 		_reclaim_telldir(DIR *);
void 		_seekdir(DIR *, long);
void		_fixtelldir(DIR *dirp, long oldseek, long oldloc);

#define	RDU_SKIP	0x0001
#define	RDU_SHORT	0x0002

#endif
