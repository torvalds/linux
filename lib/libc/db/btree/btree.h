/*	$OpenBSD: btree.h,v 1.7 2015/07/16 04:27:33 tedu Exp $	*/

/*-
 * Copyright (c) 1991, 1993, 1994
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
 *
 *	@(#)btree.h	8.11 (Berkeley) 8/17/94
 */

/* Macros to set/clear/test flags. */
#define	F_SET(p, f)	(p)->flags |= (f)
#define	F_CLR(p, f)	(p)->flags &= ~(f)
#define	F_ISSET(p, f)	((p)->flags & (f))

#include <mpool.h>

#define	DEFMINKEYPAGE	(2)		/* Minimum keys per page */
#define	MINCACHE	(5)		/* Minimum cached pages */
#define	MINPSIZE	(512)		/* Minimum page size */

/*
 * Page 0 of a btree file contains a copy of the meta-data.  This page is also
 * used as an out-of-band page, i.e. page pointers that point to nowhere point
 * to page 0.  Page 1 is the root of the btree.
 */
#define	P_INVALID	 0		/* Invalid tree page number. */
#define	P_META		 0		/* Tree metadata page number. */
#define	P_ROOT		 1		/* Tree root page number. */

/*
 * There are five page layouts in the btree: btree internal pages (BINTERNAL),
 * btree leaf pages (BLEAF), recno internal pages (RINTERNAL), recno leaf pages
 * (RLEAF) and overflow pages.  All five page types have a page header (PAGE).
 * This implementation requires that values within structures NOT be padded.
 * (ANSI C permits random padding.)  If your compiler pads randomly you'll have
 * to do some work to get this package to run.
 */
typedef struct _page {
	pgno_t	pgno;			/* this page's page number */
	pgno_t	prevpg;			/* left sibling */
	pgno_t	nextpg;			/* right sibling */

#define	P_BINTERNAL	0x01		/* btree internal page */
#define	P_BLEAF		0x02		/* leaf page */
#define	P_OVERFLOW	0x04		/* overflow page */
#define	P_RINTERNAL	0x08		/* recno internal page */
#define	P_RLEAF		0x10		/* leaf page */
#define P_TYPE		0x1f		/* type mask */
#define	P_PRESERVE	0x20		/* never delete this chain of pages */
	u_int32_t flags;

	indx_t	lower;			/* lower bound of free space on page */
	indx_t	upper;			/* upper bound of free space on page */
	indx_t	linp[1];		/* indx_t-aligned VAR. LENGTH DATA */
} PAGE;

/* First and next index. */
#define	BTDATAOFF							\
	(sizeof(pgno_t) + sizeof(pgno_t) + sizeof(pgno_t) +		\
	    sizeof(u_int32_t) + sizeof(indx_t) + sizeof(indx_t))
#define	NEXTINDEX(p)	(((p)->lower - BTDATAOFF) / sizeof(indx_t))

/*
 * For pages other than overflow pages, there is an array of offsets into the
 * rest of the page immediately following the page header.  Each offset is to
 * an item which is unique to the type of page.  The h_lower offset is just
 * past the last filled-in index.  The h_upper offset is the first item on the
 * page.  Offsets are from the beginning of the page.
 *
 * If an item is too big to store on a single page, a flag is set and the item
 * is a { page, size } pair such that the page is the first page of an overflow
 * chain with size bytes of item.  Overflow pages are simply bytes without any
 * external structure.
 *
 * The page number and size fields in the items are pgno_t-aligned so they can
 * be manipulated without copying.  (This presumes that 32 bit items can be
 * manipulated on this system.)
 */
#define	LALIGN(n)	(((n) + sizeof(pgno_t) - 1) & ~(sizeof(pgno_t) - 1))
#define	NOVFLSIZE	(sizeof(pgno_t) + sizeof(u_int32_t))

/*
 * For the btree internal pages, the item is a key.  BINTERNALs are {key, pgno}
 * pairs, such that the key compares less than or equal to all of the records
 * on that page.  For a tree without duplicate keys, an internal page with two
 * consecutive keys, a and b, will have all records greater than or equal to a
 * and less than b stored on the page associated with a.  Duplicate keys are
 * somewhat special and can cause duplicate internal and leaf page records and
 * some minor modifications of the above rule.
 */
typedef struct _binternal {
	u_int32_t ksize;		/* key size */
	pgno_t	pgno;			/* page number stored on */
#define	P_BIGDATA	0x01		/* overflow data */
#define	P_BIGKEY	0x02		/* overflow key */
	u_char	flags;
	char	bytes[1];		/* data */
} BINTERNAL;

/* Get the page's BINTERNAL structure at index indx. */
#define	GETBINTERNAL(pg, indx)						\
	((BINTERNAL *)((char *)(pg) + (pg)->linp[indx]))

/* Get the number of bytes in the entry. */
#define NBINTERNAL(len)							\
	LALIGN(sizeof(u_int32_t) + sizeof(pgno_t) + sizeof(u_char) + (len))

/* Copy a BINTERNAL entry to the page. */
#define	WR_BINTERNAL(p, size, pgno, flags) {				\
	*(u_int32_t *)p = size;						\
	p += sizeof(u_int32_t);						\
	*(pgno_t *)p = pgno;						\
	p += sizeof(pgno_t);						\
	*(u_char *)p = flags;						\
	p += sizeof(u_char);						\
}

/*
 * For the recno internal pages, the item is a page number with the number of
 * keys found on that page and below.
 */
typedef struct _rinternal {
	recno_t	nrecs;			/* number of records */
	pgno_t	pgno;			/* page number stored below */
} RINTERNAL;

/* Get the page's RINTERNAL structure at index indx. */
#define	GETRINTERNAL(pg, indx)						\
	((RINTERNAL *)((char *)(pg) + (pg)->linp[indx]))

/* Get the number of bytes in the entry. */
#define NRINTERNAL							\
	LALIGN(sizeof(recno_t) + sizeof(pgno_t))

/* Copy a RINTERAL entry to the page. */
#define	WR_RINTERNAL(p, nrecs, pgno) {					\
	*(recno_t *)p = nrecs;						\
	p += sizeof(recno_t);						\
	*(pgno_t *)p = pgno;						\
}

/* For the btree leaf pages, the item is a key and data pair. */
typedef struct _bleaf {
	u_int32_t	ksize;		/* size of key */
	u_int32_t	dsize;		/* size of data */
	u_char	flags;			/* P_BIGDATA, P_BIGKEY */
	char	bytes[1];		/* data */
} BLEAF;

/* Get the page's BLEAF structure at index indx. */
#define	GETBLEAF(pg, indx)						\
	((BLEAF *)((char *)(pg) + (pg)->linp[indx]))

/* Get the number of bytes in the entry. */
#define NBLEAF(p)	NBLEAFDBT((p)->ksize, (p)->dsize)

/* Get the number of bytes in the user's key/data pair. */
#define NBLEAFDBT(ksize, dsize)						\
	LALIGN(sizeof(u_int32_t) + sizeof(u_int32_t) + sizeof(u_char) +	\
	    (ksize) + (dsize))

/* Copy a BLEAF entry to the page. */
#define	WR_BLEAF(p, key, data, flags) {					\
	*(u_int32_t *)p = key->size;					\
	p += sizeof(u_int32_t);						\
	*(u_int32_t *)p = data->size;					\
	p += sizeof(u_int32_t);						\
	*(u_char *)p = flags;						\
	p += sizeof(u_char);						\
	memmove(p, key->data, key->size);				\
	p += key->size;							\
	memmove(p, data->data, data->size);				\
}

/* For the recno leaf pages, the item is a data entry. */
typedef struct _rleaf {
	u_int32_t	dsize;		/* size of data */
	u_char	flags;			/* P_BIGDATA */
	char	bytes[1];
} RLEAF;

/* Get the page's RLEAF structure at index indx. */
#define	GETRLEAF(pg, indx)						\
	((RLEAF *)((char *)(pg) + (pg)->linp[indx]))

/* Get the number of bytes in the entry. */
#define NRLEAF(p)	NRLEAFDBT((p)->dsize)

/* Get the number of bytes from the user's data. */
#define	NRLEAFDBT(dsize)						\
	LALIGN(sizeof(u_int32_t) + sizeof(u_char) + (dsize))

/* Copy a RLEAF entry to the page. */
#define	WR_RLEAF(p, data, flags) {					\
	*(u_int32_t *)p = data->size;					\
	p += sizeof(u_int32_t);						\
	*(u_char *)p = flags;						\
	p += sizeof(u_char);						\
	memmove(p, data->data, data->size);				\
}

/*
 * A record in the tree is either a pointer to a page and an index in the page
 * or a page number and an index.  These structures are used as a cursor, stack
 * entry and search returns as well as to pass records to other routines.
 *
 * One comment about searches.  Internal page searches must find the largest
 * record less than key in the tree so that descents work.  Leaf page searches
 * must find the smallest record greater than key so that the returned index
 * is the record's correct position for insertion.
 */
typedef struct _epgno {
	pgno_t	pgno;			/* the page number */
	indx_t	index;			/* the index on the page */
} EPGNO;

typedef struct _epg {
	PAGE	*page;			/* the (pinned) page */
	indx_t	 index;			/* the index on the page */
} EPG;

/*
 * About cursors.  The cursor (and the page that contained the key/data pair
 * that it referenced) can be deleted, which makes things a bit tricky.  If
 * there are no duplicates of the cursor key in the tree (i.e. B_NODUPS is set
 * or there simply aren't any duplicates of the key) we copy the key that it
 * referenced when it's deleted, and reacquire a new cursor key if the cursor
 * is used again.  If there are duplicates keys, we move to the next/previous
 * key, and set a flag so that we know what happened.  NOTE: if duplicate (to
 * the cursor) keys are added to the tree during this process, it is undefined
 * if they will be returned or not in a cursor scan.
 *
 * The flags determine the possible states of the cursor:
 *
 * CURS_INIT	The cursor references *something*.
 * CURS_ACQUIRE	The cursor was deleted, and a key has been saved so that
 *		we can reacquire the right position in the tree.
 * CURS_AFTER, CURS_BEFORE
 *		The cursor was deleted, and now references a key/data pair
 *		that has not yet been returned, either before or after the
 *		deleted key/data pair.
 * XXX
 * This structure is broken out so that we can eventually offer multiple
 * cursors as part of the DB interface.
 */
typedef struct _cursor {
	EPGNO	 pg;			/* B: Saved tree reference. */
	DBT	 key;			/* B: Saved key, or key.data == NULL. */
	recno_t	 rcursor;		/* R: recno cursor (1-based) */

#define	CURS_ACQUIRE	0x01		/*  B: Cursor needs to be reacquired. */
#define	CURS_AFTER	0x02		/*  B: Unreturned cursor after key. */
#define	CURS_BEFORE	0x04		/*  B: Unreturned cursor before key. */
#define	CURS_INIT	0x08		/* RB: Cursor initialized. */
	u_int8_t flags;
} CURSOR;

/*
 * The metadata of the tree.  The nrecs field is used only by the RECNO code.
 * This is because the btree doesn't really need it and it requires that every
 * put or delete call modify the metadata.
 */
typedef struct _btmeta {
	u_int32_t	magic;		/* magic number */
	u_int32_t	version;	/* version */
	u_int32_t	psize;		/* page size */
	u_int32_t	free;		/* page number of first free page */
	u_int32_t	nrecs;		/* R: number of records */

#define	SAVEMETA	(B_NODUPS | R_RECNO)
	u_int32_t	flags;		/* bt_flags & SAVEMETA */
} BTMETA;

/* The in-memory btree/recno data structure. */
typedef struct _btree {
	MPOOL	 *bt_mp;		/* memory pool cookie */

	DB	 *bt_dbp;		/* pointer to enclosing DB */

	EPG	  bt_cur;		/* current (pinned) page */
	PAGE	 *bt_pinned;		/* page pinned across calls */

	CURSOR	  bt_cursor;		/* cursor */

#define	BT_PUSH(t, p, i) {						\
	t->bt_sp->pgno = p; 						\
	t->bt_sp->index = i; 						\
	++t->bt_sp;							\
}
#define	BT_POP(t)	(t->bt_sp == t->bt_stack ? NULL : --t->bt_sp)
#define	BT_CLR(t)	(t->bt_sp = t->bt_stack)
	EPGNO	  bt_stack[50];		/* stack of parent pages */
	EPGNO	 *bt_sp;		/* current stack pointer */

	DBT	  bt_rkey;		/* returned key */
	DBT	  bt_rdata;		/* returned data */

	int	  bt_fd;		/* tree file descriptor */

	pgno_t	  bt_free;		/* next free page */
	u_int32_t bt_psize;		/* page size */
	indx_t	  bt_ovflsize;		/* cut-off for key/data overflow */
	int	  bt_lorder;		/* byte order */
					/* sorted order */
	enum { NOT, BACK, FORWARD } bt_order;
	EPGNO	  bt_last;		/* last insert */

					/* B: key comparison function */
	int	(*bt_cmp)(const DBT *, const DBT *);
					/* B: prefix comparison function */
	size_t	(*bt_pfx)(const DBT *, const DBT *);
					/* R: recno input function */
	int	(*bt_irec)(struct _btree *, recno_t);

	FILE	 *bt_rfp;		/* R: record FILE pointer */
	int	  bt_rfd;		/* R: record file descriptor */

	caddr_t	  bt_cmap;		/* R: current point in mapped space */
	caddr_t	  bt_smap;		/* R: start of mapped space */
	caddr_t   bt_emap;		/* R: end of mapped space */
	size_t	  bt_msize;		/* R: size of mapped region. */

	recno_t	  bt_nrecs;		/* R: number of records */
	size_t	  bt_reclen;		/* R: fixed record length */
	u_char	  bt_bval;		/* R: delimiting byte/pad character */

/*
 * NB:
 * B_NODUPS and R_RECNO are stored on disk, and may not be changed.
 */
#define	B_INMEM		0x00001		/* in-memory tree */
#define	B_METADIRTY	0x00002		/* need to write metadata */
#define	B_MODIFIED	0x00004		/* tree modified */
#define	B_NEEDSWAP	0x00008		/* if byte order requires swapping */
#define	B_RDONLY	0x00010		/* read-only tree */

#define	B_NODUPS	0x00020		/* no duplicate keys permitted */
#define	R_RECNO		0x00080		/* record oriented tree */

#define	R_CLOSEFP	0x00040		/* opened a file pointer */
#define	R_EOF		0x00100		/* end of input file reached. */
#define	R_FIXLEN	0x00200		/* fixed length records */
#define	R_INMEM		0x00800		/* in-memory file */
#define	R_MODIFIED	0x01000		/* modified file */
#define	R_RDONLY	0x02000		/* read-only file */

#define	B_DB_LOCK	0x04000		/* DB_LOCK specified. */
#define	B_DB_SHMEM	0x08000		/* DB_SHMEM specified. */
#define	B_DB_TXN	0x10000		/* DB_TXN specified. */
	u_int32_t flags;
} BTREE;

#include "extern.h"
