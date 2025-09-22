/*	$OpenBSD: mpool.h,v 1.1 2015/09/09 15:35:24 guenther Exp $	*/
/*	$NetBSD: mpool.h,v 1.7 1996/05/03 21:13:41 cgd Exp $	*/

/*-
 * Copyright (c) 1991, 1993, 1994
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
 *	@(#)mpool.h	8.4 (Berkeley) 11/2/95
 */

#ifndef _MPOOL_H_ 
#define _MPOOL_H_

#include <sys/queue.h>

/*
 * The memory pool scheme is a simple one.  Each in-memory page is referenced
 * by a bucket which is threaded in up to two of three ways.  All active pages
 * are threaded on a hash chain (hashed by page number) and an lru chain.
 * Inactive pages are threaded on a free chain.  Each reference to a memory
 * pool is handed an opaque MPOOL cookie which stores all of this information.
 */
#define	HASHSIZE	128
#define	HASHKEY(pgno)	((pgno - 1 + HASHSIZE) % HASHSIZE)

/* The BKT structures are the elements of the queues. */
typedef struct _bkt {
	TAILQ_ENTRY(_bkt) hq;		/* hash queue */
	TAILQ_ENTRY(_bkt) q;		/* lru queue */
	void    *page;			/* page */
	pgno_t   pgno;			/* page number */

#define	MPOOL_DIRTY	0x01		/* page needs to be written */
#define	MPOOL_PINNED	0x02		/* page is pinned into memory */
#define	MPOOL_INUSE	0x04		/* page address is valid */
	u_int8_t flags;			/* flags */
} BKT;

typedef struct MPOOL {
	TAILQ_HEAD(_lqh, _bkt) lqh;	/* lru queue head */
					/* hash queue array */
	TAILQ_HEAD(_hqh, _bkt) hqh[HASHSIZE];
	pgno_t	curcache;		/* current number of cached pages */
	pgno_t	maxcache;		/* max number of cached pages */
	pgno_t	npages;			/* number of pages in the file */
	unsigned long	pagesize;	/* file page size */
	int	fd;			/* file descriptor */
					/* page in conversion routine */
	void    (*pgin)(void *, pgno_t, void *);
					/* page out conversion routine */
	void    (*pgout)(void *, pgno_t, void *);
	void	*pgcookie;		/* cookie for page in/out routines */
#ifdef STATISTICS
	unsigned long	cachehit;
	unsigned long	cachemiss;
	unsigned long	pagealloc;
	unsigned long	pageflush;
	unsigned long	pageget;
	unsigned long	pagenew;
	unsigned long	pageput;
	unsigned long	pageread;
	unsigned long	pagewrite;
#endif
} MPOOL;

#define	MPOOL_IGNOREPIN	0x01		/* Ignore if the page is pinned. */
#define	MPOOL_PAGE_REQUEST	0x01	/* Allocate a new page with a
					   specific page number. */
#define	MPOOL_PAGE_NEXT		0x02	/* Allocate a new page with the next
					  page number. */

__BEGIN_HIDDEN_DECLS
MPOOL	*mpool_open(void *, int, pgno_t, pgno_t);
void	 mpool_filter(MPOOL *, void (*)(void *, pgno_t, void *),
	    void (*)(void *, pgno_t, void *), void *);
void	*mpool_new(MPOOL *, pgno_t *, unsigned int);
void	*mpool_get(MPOOL *, pgno_t, unsigned int);
int	 mpool_delete(MPOOL *, void *);
int	 mpool_put(MPOOL *, void *, unsigned int);
int	 mpool_sync(MPOOL *);
int	 mpool_close(MPOOL *);

PROTO_NORMAL(mpool_open);
PROTO_NORMAL(mpool_filter);
PROTO_NORMAL(mpool_new);
PROTO_NORMAL(mpool_get);
PROTO_NORMAL(mpool_delete);
PROTO_NORMAL(mpool_put);
PROTO_NORMAL(mpool_sync);
PROTO_NORMAL(mpool_close);

#ifdef STATISTICS
void	 mpool_stat(MPOOL *);
PROTO_NORMAL(mpool_stat);
#endif
__END_HIDDEN_DECLS

#endif
