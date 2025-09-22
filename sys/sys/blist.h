/* $OpenBSD: blist.h,v 1.2 2022/08/06 13:44:04 semarie Exp $ */
/* DragonFlyBSD:7b80531f545c7d3c51c1660130c71d01f6bccbe0:/sys/sys/blist.h */
/*
 * Copyright (c) 2003,2004 The DragonFly Project.  All rights reserved.
 * 
 * This code is derived from software contributed to The DragonFly Project
 * by Matthew Dillon <dillon@backplane.com>
 * 
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in
 *    the documentation and/or other materials provided with the
 *    distribution.
 * 3. Neither the name of The DragonFly Project nor the names of its
 *    contributors may be used to endorse or promote products derived
 *    from this software without specific, prior written permission.
 * 
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 * FOR A PARTICULAR PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE
 * COPYRIGHT HOLDERS OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY OR CONSEQUENTIAL DAMAGES (INCLUDING,
 * BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED
 * AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT
 * OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 * 
 * Implements bitmap resource lists.
 *
 *	Usage:
 *		blist = blist_create(blocks)
 *		(void)  blist_destroy(blist)
 *		blkno = blist_alloc(blist, count)
 *		(void)  blist_free(blist, blkno, count)
 *		nblks = blist_fill(blist, blkno, count)
 *		(void)  blist_resize(&blist, count, freeextra)
 *
 *
 *	Notes:
 *		on creation, the entire list is marked reserved.  You should
 *		first blist_free() the sections you want to make available
 *		for allocation before doing general blist_alloc()/free()
 *		ops.
 *
 *		SWAPBLK_NONE is returned on failure.  This module is typically
 *		capable of managing up to (2^31) blocks per blist, though
 *		the memory utilization would be insane if you actually did
 *		that.  Managing something like 512MB worth of 4K blocks
 *		eats around 32 KBytes of memory.
 *
 * $FreeBSD: src/sys/sys/blist.h,v 1.2.2.1 2003/01/12 09:23:12 dillon Exp $
 */

#ifndef _SYS_BLIST_H_
#define _SYS_BLIST_H_

#ifndef _SYS_TYPES_H_
#include <sys/types.h>
#endif

typedef u_long		swblk_t;
typedef u_int64_t	u_swblk_t;

/*
 * note: currently use SWAPBLK_NONE as an absolute value rather then
 * a flag bit.
 */
#define SWAPBLK_NONE	((swblk_t)-1)

/*
 * blmeta and bl_bitmap_t MUST be a power of 2 in size.
 */

typedef struct blmeta {
	union {
	    swblk_t	bmu_avail;	/* space available under us	*/
	    u_swblk_t	bmu_bitmap;	/* bitmap if we are a leaf	*/
	} u;
	swblk_t		bm_bighint;	/* biggest contiguous block hint*/
} blmeta_t;

typedef struct blist {
	swblk_t		bl_blocks;	/* area of coverage		*/
	/* XXX int64_t bl_radix */
	swblk_t		bl_radix;	/* coverage radix		*/
	swblk_t		bl_skip;	/* starting skip		*/
	swblk_t		bl_free;	/* number of free blocks	*/
	blmeta_t	*bl_root;	/* root of radix tree		*/
	swblk_t		bl_rootblks;	/* swblk_t blks allocated for tree */
} *blist_t;

#define BLIST_META_RADIX	(sizeof(u_swblk_t)*8/2)	/* 2 bits per */
#define BLIST_BMAP_RADIX	(sizeof(u_swblk_t)*8)	/* 1 bit per */

/*
 * The radix may exceed the size of a 64 bit signed (or unsigned) int
 * when the maximal number of blocks is allocated.  With a 32-bit swblk_t
 * this corresponds to ~1G x PAGE_SIZE = 4096GB.  The swap code usually
 * divides this by 4, leaving us with a capability of up to four 1TB swap
 * devices.
 *
 * With a 64-bit swblk_t the limitation is some insane number.
 *
 * NOTE: For now I don't trust that we overflow-detect properly so we divide
 *	 out to ensure that no overflow occurs.
 */

#if defined(_LP64)
/* swblk_t 64bits */
#define BLIST_MAXBLKS		(0x4000000000000000LL /		\
				 (BLIST_BMAP_RADIX / BLIST_META_RADIX))
#else
#define BLIST_MAXBLKS		(0x40000000 /		\
				 (BLIST_BMAP_RADIX / BLIST_META_RADIX))
#endif

#define BLIST_MAX_ALLOC		BLIST_BMAP_RADIX

blist_t blist_create(swblk_t);
void blist_destroy(blist_t);
swblk_t blist_alloc(blist_t, swblk_t);
swblk_t blist_allocat(blist_t, swblk_t, swblk_t);
void blist_free(blist_t, swblk_t, swblk_t);
swblk_t blist_fill(blist_t, swblk_t, swblk_t);
void blist_print(blist_t);
void blist_resize(blist_t *, swblk_t, int);
void blist_gapfind(blist_t, swblk_t *, swblk_t *);

#endif	/* _SYS_BLIST_H_ */
