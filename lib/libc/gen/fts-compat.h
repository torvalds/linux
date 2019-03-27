/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 1989, 1993
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
 *
 *	@(#)fts.h	8.3 (Berkeley) 8/14/94
 * $FreeBSD$
 */

#ifndef	_FTS_H_
#define	_FTS_H_

typedef struct {
	struct _ftsent *fts_cur;	/* current node */
	struct _ftsent *fts_child;	/* linked list of children */
	struct _ftsent **fts_array;	/* sort array */
	uint32_t fts_dev;		/* starting device # */
	char *fts_path;			/* path for this descent */
	int fts_rfd;			/* fd for root */
	int fts_pathlen;		/* sizeof(path) */
	int fts_nitems;			/* elements in the sort array */
	int (*fts_compar)		/* compare function */
	    (const struct _ftsent * const *, const struct _ftsent * const *);

#define	FTS_COMFOLLOW	0x001		/* follow command line symlinks */
#define	FTS_LOGICAL	0x002		/* logical walk */
#define	FTS_NOCHDIR	0x004		/* don't change directories */
#define	FTS_NOSTAT	0x008		/* don't get stat info */
#define	FTS_PHYSICAL	0x010		/* physical walk */
#define	FTS_SEEDOT	0x020		/* return dot and dot-dot */
#define	FTS_XDEV	0x040		/* don't cross devices */
#define	FTS_WHITEOUT	0x080		/* return whiteout information */
#define	FTS_OPTIONMASK	0x0ff		/* valid user option mask */

#define	FTS_NAMEONLY	0x100		/* (private) child names only */
#define	FTS_STOP	0x200		/* (private) unrecoverable error */
	int fts_options;		/* fts_open options, global flags */
	void *fts_clientptr;		/* thunk for sort function */
} FTS;

typedef struct _ftsent {
	struct _ftsent *fts_cycle;	/* cycle node */
	struct _ftsent *fts_parent;	/* parent directory */
	struct _ftsent *fts_link;	/* next file in directory */
	union {
		struct {
			long __fts_number;	/* local numeric value */
			void *__fts_pointer;	/* local address value */
		} __struct_ftsent;
		int64_t __fts_bignum;
	} __union_ftsent;
#define	fts_number	__union_ftsent.__struct_ftsent.__fts_number
#define	fts_pointer	__union_ftsent.__struct_ftsent.__fts_pointer
#define	fts_bignum	__union_ftsent.__fts_bignum
	char *fts_accpath;		/* access path */
	char *fts_path;			/* root path */
	int fts_errno;			/* errno for this node */
	int fts_symfd;			/* fd for symlink */
	u_short fts_pathlen;		/* strlen(fts_path) */
	u_short fts_namelen;		/* strlen(fts_name) */

	uint32_t fts_ino;		/* inode */
	uint32_t fts_dev;		/* device */
	uint16_t fts_nlink;		/* link count */

#define	FTS_ROOTPARENTLEVEL	-1
#define	FTS_ROOTLEVEL		 0
	short fts_level;		/* depth (-1 to N) */

#define	FTS_D		 1		/* preorder directory */
#define	FTS_DC		 2		/* directory that causes cycles */
#define	FTS_DEFAULT	 3		/* none of the above */
#define	FTS_DNR		 4		/* unreadable directory */
#define	FTS_DOT		 5		/* dot or dot-dot */
#define	FTS_DP		 6		/* postorder directory */
#define	FTS_ERR		 7		/* error; errno is set */
#define	FTS_F		 8		/* regular file */
#define	FTS_INIT	 9		/* initialized only */
#define	FTS_NS		10		/* stat(2) failed */
#define	FTS_NSOK	11		/* no stat(2) requested */
#define	FTS_SL		12		/* symbolic link */
#define	FTS_SLNONE	13		/* symbolic link without target */
#define	FTS_W		14		/* whiteout object */
	u_short fts_info;		/* user flags for FTSENT structure */

#define	FTS_DONTCHDIR	 0x01		/* don't chdir .. to the parent */
#define	FTS_SYMFOLLOW	 0x02		/* followed a symlink to get here */
#define	FTS_ISW		 0x04		/* this is a whiteout object */
	u_short fts_flags;		/* private flags for FTSENT structure */

#define	FTS_AGAIN	 1		/* read node again */
#define	FTS_FOLLOW	 2		/* follow symbolic link */
#define	FTS_NOINSTR	 3		/* no instructions */
#define	FTS_SKIP	 4		/* discard node */
	u_short fts_instr;		/* fts_set() instructions */

	struct freebsd11_stat *fts_statp; /* stat(2) information */
	char *fts_name;			/* file name */
	FTS *fts_fts;			/* back pointer to main FTS */
} FTSENT;

#define	 fts_get_clientptr(fts)	((fts)->fts_clientptr)
#define	 fts_get_stream(ftsent)	((ftsent)->fts_fts)

#endif /* !_FTS_H_ */
