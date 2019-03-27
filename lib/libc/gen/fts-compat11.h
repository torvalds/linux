/*
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

#ifndef	_FTS_COPMAT11_H_
#define	_FTS_COPMAT11_H_

typedef struct {
	struct _ftsent11 *fts_cur;	/* current node */
	struct _ftsent11 *fts_child;	/* linked list of children */
	struct _ftsent11 **fts_array;	/* sort array */
	uint32_t fts_dev;		/* starting device # */
	char *fts_path;			/* path for this descent */
	int fts_rfd;			/* fd for root */
	__size_t fts_pathlen;		/* sizeof(path) */
	__size_t fts_nitems;		/* elements in the sort array */
	int (*fts_compar)		/* compare function */
	    (const struct _ftsent11 * const *,
	    const struct _ftsent11 * const *);
	int fts_options;		/* fts_open options, global flags */
	void *fts_clientptr;		/* thunk for sort function */
} FTS11;

typedef struct _ftsent11 {
	struct _ftsent11 *fts_cycle;	/* cycle node */
	struct _ftsent11 *fts_parent;	/* parent directory */
	struct _ftsent11 *fts_link;	/* next file in directory */
	long long fts_number;		/* local numeric value */
	void *fts_pointer;		/* local address value */
	char *fts_accpath;		/* access path */
	char *fts_path;			/* root path */
	int fts_errno;			/* errno for this node */
	int fts_symfd;			/* fd for symlink */
	__size_t fts_pathlen;		/* strlen(fts_path) */
	__size_t fts_namelen;		/* strlen(fts_name) */

	uint32_t fts_ino;		/* inode */
	uint32_t fts_dev;		/* device */
	uint16_t fts_nlink;		/* link count */

	long fts_level;			/* depth (-1 to N) */

	int fts_info;			/* user status for FTSENT structure */

	unsigned fts_flags;		/* private flags for FTSENT structure */

	int fts_instr;			/* fts_set() instructions */

	struct freebsd11_stat *fts_statp; /* stat(2) information */
	char *fts_name;			/* file name */
	FTS11 *fts_fts;			/* back pointer to main FTS */
} FTSENT11;

FTSENT11	*freebsd11_fts_children(FTS11 *, int);
int		 freebsd11_fts_close(FTS11 *);
void		*freebsd11_fts_get_clientptr(FTS11 *);
#define	freebsd11_fts_get_clientptr(fts)	((fts)->fts_clientptr)
FTS11		*freebsd11_fts_get_stream(FTSENT11 *);
#define	freebsd11_fts_get_stream(ftsent)	((ftsent)->fts_fts)
FTS11		*freebsd11_fts_open(char * const *, int,
		    int (*)(const FTSENT11 * const *,
		    const FTSENT11 * const *));
FTSENT11	*freebsd11_fts_read(FTS11 *);
int		 freebsd11_fts_set(FTS11 *, FTSENT11 *, int);
void		 freebsd11_fts_set_clientptr(FTS11 *, void *);

#endif /* !_FTS_COMPAT11_H_ */
