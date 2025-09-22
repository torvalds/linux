/*	$OpenBSD: extent.h,v 1.15 2024/01/19 22:12:24 kettenis Exp $	*/
/*	$NetBSD: extent.h,v 1.6 1997/10/09 07:43:05 jtc Exp $	*/

/*-
 * Copyright (c) 1996 The NetBSD Foundation, Inc.
 * All rights reserved.
 *
 * This code is derived from software contributed to The NetBSD Foundation
 * by Jason R. Thorpe.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE NETBSD FOUNDATION, INC. AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
 * TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE FOUNDATION OR CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#ifndef _SYS_EXTENT_H_
#define _SYS_EXTENT_H_

#include <sys/queue.h>

struct extent_region {
	LIST_ENTRY(extent_region) er_link;	/* link in region list */
	u_long 	er_start;		/* start of region */
	u_long	er_end;			/* end of region */
	int	er_flags;		/* misc. flags */
};

/* er_flags */
#define ER_ALLOC	0x01	/* region descriptor dynamically allocated */
#define ER_DISCARD	0x02	/* discard region descriptor after use */

struct extent {
	char	*ex_name;		/* name of extent */
					/* allocated regions in extent */
	LIST_HEAD(, extent_region) ex_regions;
	u_long	ex_start;		/* start of extent */
	u_long	ex_end;			/* end of extent */
	int	ex_mtype;		/* memory type */
	int	ex_flags;		/* misc. information */

	LIST_ENTRY(extent) ex_link;
};

struct extent_fixed {
	struct extent	fex_extent;	/* MUST BE FIRST */
					/* freelist of region descriptors */
	LIST_HEAD(, extent_region) fex_freelist;
	caddr_t		fex_storage;	/* storage space for descriptors */
	size_t		fex_storagesize; /* size of storage space */
};

/* ex_flags; for internal use only */
#define EXF_FIXED	0x01		/* extent uses fixed storage */
#define EXF_NOCOALESCE	0x02		/* coalescing of regions not allowed */
#define EXF_WANTED	0x04		/* someone asleep on extent */
#define EXF_FLWANTED	0x08		/* someone asleep on freelist */

#define EXF_BITS	"\20\4FLWANTED\3WANTED\2NOCOALESCE\1FIXED"

/* misc. flags passed to extent functions */
#define EX_NOWAIT	0x0000		/* not safe to sleep */
#define EX_WAITOK	0x0001		/* safe to sleep */
#define EX_FAST		0x0002		/* take first fit in extent_alloc() */
#define EX_CATCH	0x0004		/* catch signals while sleeping */
#define EX_NOCOALESCE	0x0008		/* create a non-coalescing extent */
#define EX_MALLOCOK	0x0010		/* safe to call malloc() */
#define EX_WAITSPACE	0x0020		/* wait for space to become free */
#define EX_BOUNDZERO	0x0040		/* boundary lines start at 0 */
#define EX_CONFLICTOK	0x0080		/* allow conflicts */
#define EX_FILLED	0x0100		/* create a filled extent */

/*
 * Special place holders for "alignment" and "boundary" arguments,
 * in the event the caller doesn't wish to use those features.
 */
#define EX_NOALIGN	1		/* don't do alignment */
#define EX_NOBOUNDARY	0		/* don't do boundary checking */

#if defined(_KERNEL) || defined(_EXTENT_TESTING)
#define EXTENT_FIXED_STORAGE_SIZE(_nregions)		\
	(ALIGN(sizeof(struct extent_fixed)) +		\
	((ALIGN(sizeof(struct extent_region))) *	\
	 (_nregions)))

void extent_print_all(void);

struct	extent *extent_create(char *, u_long, u_long, int,
	    caddr_t, size_t, int);
void	extent_destroy(struct extent *);
int	extent_alloc_subregion(struct extent *, u_long, u_long,
	    u_long, u_long, u_long, u_long, int, u_long *);
int	extent_alloc_subregion_with_descr(struct extent *, u_long, u_long,
	    u_long, u_long, u_long, u_long, int, struct extent_region *,
	    u_long *);
int	extent_alloc_region(struct extent *, u_long, u_long, int);
int	extent_alloc_region_with_descr(struct extent *, u_long, u_long,
	    int, struct extent_region *);
int	extent_free(struct extent *, u_long, u_long, int);
void	extent_print(struct extent *);

/* Simple case of extent_alloc_subregion() */
#define extent_alloc(_ex, _size, _alignment, _skew, _boundary, \
		_flags, _result) \
	extent_alloc_subregion((_ex), (_ex)->ex_start, (_ex)->ex_end,  \
	(_size), (_alignment), (_skew), (_boundary), (_flags), (_result))

/* Simple case of extent_alloc_subregion_with_descr() */
#define extent_alloc_with_descr(_ex, _size, _alignment, _skew, _boundary, \
		_flags, _region, _result) \
	extent_alloc_subregion_with_descr((_ex), (_ex)->ex_start, \
	(_ex)->ex_end, (_size), (_alignment), (_skew), (_boundary), \
	(_flags), (_region), (_result))
#endif /* _KERNEL || _EXTENT_TESTING */

#endif /* ! _SYS_EXTENT_H_ */
