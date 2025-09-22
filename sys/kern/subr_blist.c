/* $OpenBSD: subr_blist.c,v 1.4 2023/05/30 08:30:01 jsg Exp $ */
/* DragonFlyBSD:7b80531f545c7d3c51c1660130c71d01f6bccbe0:/sys/kern/subr_blist.c */
/*
 * BLIST.C -	Bitmap allocator/deallocator, using a radix tree with hinting
 * 
 * Copyright (c) 1998,2004 The DragonFly Project.  All rights reserved.
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
 *
 *	This module implements a general bitmap allocator/deallocator.  The
 *	allocator eats around 2 bits per 'block'.  The module does not
 *	try to interpret the meaning of a 'block' other than to return
 *	SWAPBLK_NONE on an allocation failure.
 *
 *	A radix tree is used to maintain the bitmap.  Two radix constants are
 *	involved:  One for the bitmaps contained in the leaf nodes (typically
 *	32), and one for the meta nodes (typically 16).  Both meta and leaf
 *	nodes have a hint field.  This field gives us a hint as to the largest
 *	free contiguous range of blocks under the node.  It may contain a
 *	value that is too high, but will never contain a value that is too
 *	low.  When the radix tree is searched, allocation failures in subtrees
 *	update the hint.
 *
 *	The radix tree also implements two collapsed states for meta nodes:
 *	the ALL-ALLOCATED state and the ALL-FREE state.  If a meta node is
 *	in either of these two states, all information contained underneath
 *	the node is considered stale.  These states are used to optimize
 *	allocation and freeing operations.
 *
 * 	The hinting greatly increases code efficiency for allocations while
 *	the general radix structure optimizes both allocations and frees.  The
 *	radix tree should be able to operate well no matter how much
 *	fragmentation there is and no matter how large a bitmap is used.
 *
 *	Unlike the rlist code, the blist code wires all necessary memory at
 *	creation time.  Neither allocations nor frees require interaction with
 *	the memory subsystem.  In contrast, the rlist code may allocate memory
 *	on an blist_free() call.  The non-blocking features of the blist code
 *	are used to great advantage in the swap code (uvm/uvm_swap.c).  The
 *	rlist code uses a little less overall memory than the blist code (but
 *	due to swap interleaving not all that much less), but the blist code
 *	scales much, much better.
 *
 *	LAYOUT: The radix tree is laid out recursively using a
 *	linear array.  Each meta node is immediately followed (laid out
 *	sequentially in memory) by BLIST_META_RADIX lower level nodes.  This
 *	is a recursive structure but one that can be easily scanned through
 *	a very simple 'skip' calculation.  In order to support large radixes,
 *	portions of the tree may reside outside our memory allocation.  We
 *	handle this with an early-termination optimization (when bighint is
 *	set to -1) on the scan.  The memory allocation is only large enough
 *	to cover the number of blocks requested at creation time even if it
 *	must be encompassed in larger root-node radix.
 *
 *	NOTE: The allocator cannot currently allocate more than
 *	BLIST_BMAP_RADIX blocks per call.  It will panic with 'allocation too
 *	large' if you try.  This is an area that could use improvement.  The
 *	radix is large enough that this restriction does not effect the swap
 *	system, though.  Currently only the allocation code is effected by
 *	this algorithmic unfeature.  The freeing code can handle arbitrary
 *	ranges.
 *
 *	NOTE: The radix may exceed BLIST_BMAP_RADIX bits in order to support
 *	      up to 2^(BLIST_BMAP_RADIX-1) blocks.  The first division will
 *	      drop the radix down and fit it within a signed BLIST_BMAP_RADIX
 *	      bit integer.
 *
 *	This code can be compiled stand-alone for debugging.
 */

#ifdef _KERNEL

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/blist.h>
#include <sys/malloc.h>

#else

#ifndef BLIST_NO_DEBUG
#define BLIST_DEBUG
#endif

#include <sys/types.h>
#include <assert.h>
#include <err.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdarg.h>
#include <limits.h>

#define malloc(s,t,f)	calloc(1, s)
#define mallocarray(n,s,t,f)	reallocarray(NULL, n, s)
#define free(p,t,s)	free(p)
#define KASSERT(exp)	assert(exp)
#define KDASSERT(exp)	assert(exp)

#include "../sys/blist.h"

#define panic(...)	do { errx(1, __VA_ARGS__); } while (0)

#endif

/*
 * static support functions
 */

static swblk_t blst_leaf_alloc(blmeta_t *scan, swblk_t blkat,
				swblk_t blk, swblk_t count);
static swblk_t blst_meta_alloc(blmeta_t *scan, swblk_t blkat,
				swblk_t blk, swblk_t count,
				swblk_t radix, swblk_t skip);
static void blst_leaf_free(blmeta_t *scan, swblk_t relblk, swblk_t count);
static void blst_meta_free(blmeta_t *scan, swblk_t freeBlk, swblk_t count, 
					swblk_t radix, swblk_t skip,
					swblk_t blk);
static swblk_t blst_leaf_fill(blmeta_t *scan, swblk_t blk, swblk_t count);
static swblk_t blst_meta_fill(blmeta_t *scan, swblk_t fillBlk, swblk_t count,
					swblk_t radix, swblk_t skip,
					swblk_t blk);
static void blst_copy(blmeta_t *scan, swblk_t blk, swblk_t radix,
				swblk_t skip, blist_t dest, swblk_t count);
static swblk_t	blst_radix_init(blmeta_t *scan, swblk_t radix,
						swblk_t skip, swblk_t count);
static int blst_radix_gapfind(blmeta_t *scan, swblk_t blk, swblk_t radix, swblk_t skip,
    int state, swblk_t *maxbp, swblk_t *maxep, swblk_t *bp, swblk_t *ep);

#if defined(BLIST_DEBUG) || defined(DDB)
static void	blst_radix_print(blmeta_t *scan, swblk_t blk,
					swblk_t radix, swblk_t skip, int tab);
#endif

/*
 * blist_create() - create a blist capable of handling up to the specified
 *		    number of blocks
 *
 *	blocks must be greater than 0
 *
 *	The smallest blist consists of a single leaf node capable of
 *	managing BLIST_BMAP_RADIX blocks.
 *
 *	The pages are addressable in range [0, nblocks[
 */

blist_t
blist_create(swblk_t blocks)
{
	blist_t bl;
	swblk_t radix;
	swblk_t skip = 0;

	KASSERT(blocks > 0);

	/*
	 * Calculate radix and skip field used for scanning.
	 *
	 * Radix can exceed BLIST_BMAP_RADIX bits even if swblk_t is limited
	 * to BLIST_BMAP_RADIX bits.
	 *
	 * XXX check overflow
	 */
	radix = BLIST_BMAP_RADIX;

	while (radix < blocks) {
		radix *= BLIST_META_RADIX;
		skip = (skip + 1) * BLIST_META_RADIX;
		KASSERT(skip > 0);
	}

	bl = malloc(sizeof(struct blist), M_VMSWAP, M_WAITOK | M_ZERO);

	bl->bl_blocks = blocks;
	bl->bl_radix = radix;
	bl->bl_skip = skip;
	bl->bl_rootblks = 1 +
	    blst_radix_init(NULL, bl->bl_radix, bl->bl_skip, blocks);
	bl->bl_root = mallocarray(bl->bl_rootblks, sizeof(blmeta_t),
			      M_VMSWAP, M_WAITOK);

#if defined(BLIST_DEBUG)
	printf(
		"BLIST representing %lu blocks (%lu MB of swap)"
		", requiring %6.2fM of ram\n",
		bl->bl_blocks,
		bl->bl_blocks * 4 / 1024,
		(bl->bl_rootblks * sizeof(blmeta_t) + 1023) / (1024.0 * 1024.0)
	);
	printf("BLIST raw radix tree: %lu records, top-radix %lu\n",
		bl->bl_rootblks, bl->bl_radix);
#endif
	blst_radix_init(bl->bl_root, bl->bl_radix, bl->bl_skip, blocks);

	return(bl);
}

void
blist_destroy(blist_t bl)
{
	KASSERT(bl != NULL);

	free(bl->bl_root, M_VMSWAP, sizeof(blmeta_t) * bl->bl_rootblks);
	free(bl, M_VMSWAP, sizeof(struct blist));
}

/*
 * blist_alloc() - reserve space in the block bitmap.  Return the base
 *		     of a contiguous region or SWAPBLK_NONE if space could
 *		     not be allocated.
 */

swblk_t
blist_alloc(blist_t bl, swblk_t count)
{
	swblk_t blk = SWAPBLK_NONE;

	if (bl) {
		if (bl->bl_radix == BLIST_BMAP_RADIX)
			blk = blst_leaf_alloc(bl->bl_root, 0, 0, count);
		else
			blk = blst_meta_alloc(bl->bl_root, 0, 0, count,
					      bl->bl_radix, bl->bl_skip);
		if (blk != SWAPBLK_NONE) {
			bl->bl_free -= count;

			KDASSERT(blk < bl->bl_blocks);
			KDASSERT(bl->bl_free <= bl->bl_blocks);
		}
	}
	return(blk);
}

swblk_t
blist_allocat(blist_t bl, swblk_t count, swblk_t blkat)
{
	swblk_t blk = SWAPBLK_NONE;

	if (bl) {
		KDASSERT(blkat < bl->bl_blocks);
		KDASSERT(blkat + count <= bl->bl_blocks);

		if (bl->bl_radix == BLIST_BMAP_RADIX)
			blk = blst_leaf_alloc(bl->bl_root, blkat, 0, count);
		else
			blk = blst_meta_alloc(bl->bl_root, blkat, 0, count,
					      bl->bl_radix, bl->bl_skip);
		if (blk != SWAPBLK_NONE) {
			bl->bl_free -= count;

			KDASSERT(blk < bl->bl_blocks);
			KDASSERT(bl->bl_free <= bl->bl_blocks);
		}
	}
	return(blk);
}

/*
 * blist_free() -	free up space in the block bitmap.  Return the base
 *		     	of a contiguous region.  Panic if an inconsistency is
 *			found.
 */

void
blist_free(blist_t bl, swblk_t blkno, swblk_t count)
{
	if (bl) {
		KDASSERT(blkno < bl->bl_blocks);
		KDASSERT(blkno + count <= bl->bl_blocks);

		if (bl->bl_radix == BLIST_BMAP_RADIX)
			blst_leaf_free(bl->bl_root, blkno, count);
		else
			blst_meta_free(bl->bl_root, blkno, count, bl->bl_radix, bl->bl_skip, 0);
		bl->bl_free += count;

		KDASSERT(bl->bl_free <= bl->bl_blocks);
	}
}

/*
 * blist_fill() -	mark a region in the block bitmap as off-limits
 *			to the allocator (i.e. allocate it), ignoring any
 *			existing allocations.  Return the number of blocks
 *			actually filled that were free before the call.
 */

swblk_t
blist_fill(blist_t bl, swblk_t blkno, swblk_t count)
{
	swblk_t filled;

	if (bl) {
		KDASSERT(blkno < bl->bl_blocks);
		KDASSERT(blkno + count <= bl->bl_blocks);

		if (bl->bl_radix == BLIST_BMAP_RADIX) {
			filled = blst_leaf_fill(bl->bl_root, blkno, count);
		} else {
			filled = blst_meta_fill(bl->bl_root, blkno, count,
			    bl->bl_radix, bl->bl_skip, 0);
		}
		bl->bl_free -= filled;
		KDASSERT(bl->bl_free <= bl->bl_blocks);
		return (filled);
	} else {
		return 0;
	}
}

/*
 * blist_resize() -	resize an existing radix tree to handle the
 *			specified number of blocks.  This will reallocate
 *			the tree and transfer the previous bitmap to the new
 *			one.  When extending the tree you can specify whether
 *			the new blocks are to left allocated or freed.
 */

void
blist_resize(blist_t *pbl, swblk_t count, int freenew)
{
    blist_t newbl = blist_create(count);
    blist_t save = *pbl;

    *pbl = newbl;
    if (count > save->bl_blocks)
	    count = save->bl_blocks;
    blst_copy(save->bl_root, 0, save->bl_radix, save->bl_skip, newbl, count);

    /*
     * If resizing upwards, should we free the new space or not?
     */
    if (freenew && count < newbl->bl_blocks) {
	    blist_free(newbl, count, newbl->bl_blocks - count);
    }
    blist_destroy(save);
}

#define	GAPFIND_FIRSTFREE 0
#define	GAPFIND_FIRSTUSED 1

/*
 * blist_gapfind() -	return the largest gap (free pages) in blist.
 *			the blist isn't modified. the returned range
 *			is [maxbp, maxep[ . The size of the gap is
 *			maxep - maxbp. If not found, the size is 0.
 */

void
blist_gapfind(blist_t bl, swblk_t *maxbp, swblk_t *maxep)
{
	int state;
	swblk_t b, e;

	/* initialize gaps (max and current) */
	*maxbp = *maxep = 0;
	b = e = 0;

	/* search the larger gap from block 0 */
	state = blst_radix_gapfind(bl->bl_root, 0, bl->bl_radix, bl->bl_skip,
	    GAPFIND_FIRSTFREE, maxbp, maxep, &b, &e);

	if (state == GAPFIND_FIRSTUSED) {
		e = bl->bl_blocks;
		if (*maxep - *maxbp < e - b) {
			*maxbp = b;
			*maxep = e;
		}
	}

	KDASSERT(*maxbp <= *maxep);
	KDASSERT(*maxbp <  bl->bl_blocks);
	KDASSERT(*maxep <= bl->bl_blocks);
}

/*
 * blst_radix_gapfind -	search the larger gap in one pass
 *
 * - search first free block, from X -> set B
 * - search first used block, from B -> set E
 * - if the size (E - B) is larger than max, update it
 * - loop (with X=E) until end of blist
 * - max is the larger free gap
 */
static int
blst_radix_gapfind(blmeta_t *scan, swblk_t blk, swblk_t radix, swblk_t skip,
    int state, swblk_t *maxbp, swblk_t *maxep, swblk_t *bp, swblk_t *ep)
{
	swblk_t i;
	swblk_t next_skip;

	if (radix == BLIST_BMAP_RADIX) {
		/* leaf node: we consider only completely free bitmaps as free */
		if (state == GAPFIND_FIRSTFREE) {
			if (scan->u.bmu_bitmap == (u_swblk_t)-1) {
				/* node is fully free */
				*bp = blk;
				return GAPFIND_FIRSTUSED;
			}

			/* it isn't fully free, not found, keep state */
			return state;

		} else if (state == GAPFIND_FIRSTUSED) {
			if (scan->u.bmu_bitmap == (u_swblk_t)-1) {
				/* it is free, not found, keep state */
				return state;
			}

			/* it is (at least partially) used */
			*ep = blk;
			if (*maxep - *maxbp < *ep - *bp) {
				*maxbp = *bp;
				*maxep = *ep;
			}
			return GAPFIND_FIRSTFREE;
		}
	}

	if (scan->u.bmu_avail == 0) {
		/* ALL-ALLOCATED */
		if (state == GAPFIND_FIRSTFREE) {
			/* searching free block, not found, keep state */
			return state;

		} else if (state == GAPFIND_FIRSTUSED) {
			/* searching used block, found */
			*ep = blk;
			if (*maxep - *maxbp < *ep - *bp) {
				*maxbp = *bp;
				*maxep = *ep;
			}
			return GAPFIND_FIRSTFREE;
		}
	}

	if (scan->u.bmu_avail == radix) {
		/* ALL-FREE */
		if (state == GAPFIND_FIRSTFREE) {
			/* searching free block, found */
			*bp = blk;
			return GAPFIND_FIRSTUSED;

		} else if (state == GAPFIND_FIRSTUSED) {
			/* searching used block, not found, keep state */
			return state;
		}
	}

	radix /= BLIST_META_RADIX;
	next_skip = (skip / BLIST_META_RADIX);

	for (i = 1; i <= skip; i += next_skip) {
		if (scan[i].bm_bighint == (swblk_t)-1)
			/* Terminator */
			break;

		state = blst_radix_gapfind(&scan[i], blk, radix, next_skip - 1,
		    state, maxbp, maxep, bp, ep);

		blk += radix;
	}

	return state;
}

#if defined(BLIST_DEBUG) || defined(DDB)

/*
 * blist_print()    - dump radix tree
 */

void
blist_print(blist_t bl)
{
	printf("BLIST {\n");
	blst_radix_print(bl->bl_root, 0, bl->bl_radix, bl->bl_skip, 4);
	printf("}\n");
}

#endif

/************************************************************************
 *			  ALLOCATION SUPPORT FUNCTIONS			*
 ************************************************************************
 *
 *	These support functions do all the actual work.  They may seem
 *	rather longish, but that's because I've commented them up.  The
 *	actual code is straight forward.
 *
 */

/*
 * blist_leaf_alloc() -	allocate at a leaf in the radix tree (a bitmap).
 *
 *	This is the core of the allocator and is optimized for the 1 block
 *	and the BLIST_BMAP_RADIX block allocation cases.  Other cases are
 *	somewhat slower.  The 1 block allocation case is log2 and extremely
 *	quick.
 */

static swblk_t
blst_leaf_alloc(blmeta_t *scan, swblk_t blkat __unused, swblk_t blk,
		swblk_t count)
{
	u_swblk_t orig = scan->u.bmu_bitmap;

	if (orig == 0) {
		/*
		 * Optimize bitmap all-allocated case.  Also, count = 1
		 * case assumes at least 1 bit is free in the bitmap, so
		 * we have to take care of this case here.
		 */
		scan->bm_bighint = 0;
		return(SWAPBLK_NONE);
	}
	if (count == 1) {
		/*
		 * Optimized code to allocate one bit out of the bitmap
		 */
		u_swblk_t mask;
		int j = BLIST_BMAP_RADIX/2;
		int r = 0;

		mask = (u_swblk_t)-1 >> (BLIST_BMAP_RADIX/2);

		while (j) {
			if ((orig & mask) == 0) {
			    r += j;
			    orig >>= j;
			}
			j >>= 1;
			mask >>= j;
		}
		scan->u.bmu_bitmap &= ~((u_swblk_t)1 << r);
		return(blk + r);
	}
	if (count <= BLIST_BMAP_RADIX) {
		/*
		 * non-optimized code to allocate N bits out of the bitmap.
		 * The more bits, the faster the code runs.  It will run
		 * the slowest allocating 2 bits, but since there aren't any
		 * memory ops in the core loop (or shouldn't be, anyway),
		 * you probably won't notice the difference.
		 */
		int j;
		int n = (int)(BLIST_BMAP_RADIX - count);
		u_swblk_t mask;

		mask = (u_swblk_t)-1 >> n;

		for (j = 0; j <= n; ++j) {
			if ((orig & mask) == mask) {
				scan->u.bmu_bitmap &= ~mask;
				return(blk + j);
			}
			mask = (mask << 1);
		}
	}

	/*
	 * We couldn't allocate count in this subtree, update bighint.
	 */
	scan->bm_bighint = count - 1;

	return(SWAPBLK_NONE);
}

/*
 * blist_meta_alloc() -	allocate at a meta in the radix tree.
 *
 *	Attempt to allocate at a meta node.  If we can't, we update
 *	bighint and return a failure.  Updating bighint optimize future
 *	calls that hit this node.  We have to check for our collapse cases
 *	and we have a few optimizations strewn in as well.
 */
static swblk_t
blst_meta_alloc(blmeta_t *scan, swblk_t blkat,
		swblk_t blk, swblk_t count,
		swblk_t radix, swblk_t skip)
{
	int hintok = (blk >= blkat);
	swblk_t next_skip = ((swblk_t)skip / BLIST_META_RADIX);
	swblk_t i;

#ifndef _KERNEL
	printf("blist_meta_alloc blkat %lu blk %lu count %lu radix %lu\n",
		blkat, blk, count, radix);
#endif

	/*
	 * ALL-ALLOCATED special case
	 */
	if (scan->u.bmu_avail == 0)  {
		scan->bm_bighint = 0;
		return(SWAPBLK_NONE);
	}

	/*
	 * ALL-FREE special case, initialize uninitialized
	 * sublevel.
	 *
	 * NOTE: radix may exceed 32 bits until first division.
	 */
	if (scan->u.bmu_avail == radix) {
		scan->bm_bighint = radix;

		radix /= BLIST_META_RADIX;
		for (i = 1; i <= skip; i += next_skip) {
			if (scan[i].bm_bighint == (swblk_t)-1)
				break;
			if (next_skip == 1) {
				scan[i].u.bmu_bitmap = (u_swblk_t)-1;
				scan[i].bm_bighint = BLIST_BMAP_RADIX;
			} else {
				scan[i].bm_bighint = (swblk_t)radix;
				scan[i].u.bmu_avail = (swblk_t)radix;
			}
		}
	} else {
		radix /= BLIST_META_RADIX;
	}

	for (i = 1; i <= skip; i += next_skip) {
		if (scan[i].bm_bighint == (swblk_t)-1) {
			/*
			 * Terminator
			 *
			 * note: check it first, as swblk_t may be unsigned.
			 *   otherwise, the second if() might match and the
			 *   Terminator will be ignored.
			 */
			break;
		}

		if (count <= scan[i].bm_bighint &&
		    blk + (swblk_t)radix > blkat) {
			/*
			 * count fits in object
			 */
			swblk_t r;
			if (next_skip == 1) {
				r = blst_leaf_alloc(&scan[i], blkat,
						    blk, count);
			} else {
				r = blst_meta_alloc(&scan[i], blkat,
						    blk, count,
						    radix, next_skip - 1);
			}
			if (r != SWAPBLK_NONE) {
				scan->u.bmu_avail -= count;
				if (scan->bm_bighint > scan->u.bmu_avail)
					scan->bm_bighint = scan->u.bmu_avail;
				return(r);
			}
			/* bighint was updated by recursion */
		} else if (count > (swblk_t)radix) {
			/*
			 * count does not fit in object even if it were
			 * complete free.
			 */
			panic("%s: allocation too large %lu/%lu",
			      __func__, count, radix);
		}
		blk += (swblk_t)radix;
	}

	/*
	 * We couldn't allocate count in this subtree, update bighint.
	 */
	if (hintok && scan->bm_bighint >= count)
		scan->bm_bighint = count - 1;
	return(SWAPBLK_NONE);
}

/*
 * BLST_LEAF_FREE() -	free allocated block from leaf bitmap
 */
static void
blst_leaf_free(blmeta_t *scan, swblk_t blk, swblk_t count)
{
	/*
	 * free some data in this bitmap
	 *
	 * e.g.
	 *	0000111111111110000
	 *          \_________/\__/
	 *		v        n
	 */
	int n = blk & (BLIST_BMAP_RADIX - 1);
	u_swblk_t mask;

	mask = ((u_swblk_t)-1 << n) &
	    ((u_swblk_t)-1 >> (BLIST_BMAP_RADIX - count - n));

	if (scan->u.bmu_bitmap & mask)
		panic("%s: freeing free block", __func__);
	scan->u.bmu_bitmap |= mask;

	/*
	 * We could probably do a better job here.  We are required to make
	 * bighint at least as large as the biggest contiguous block of
	 * data.  If we just shoehorn it, a little extra overhead will
	 * be incurred on the next allocation (but only that one typically).
	 */
	scan->bm_bighint = BLIST_BMAP_RADIX;
}

/*
 * BLST_META_FREE() - free allocated blocks from radix tree meta info
 *
 *	This support routine frees a range of blocks from the bitmap.
 *	The range must be entirely enclosed by this radix node.  If a
 *	meta node, we break the range down recursively to free blocks
 *	in subnodes (which means that this code can free an arbitrary
 *	range whereas the allocation code cannot allocate an arbitrary
 *	range).
 */

static void
blst_meta_free(blmeta_t *scan, swblk_t freeBlk, swblk_t count,
	       swblk_t radix, swblk_t skip, swblk_t blk)
{
	swblk_t i;
	swblk_t next_skip = ((swblk_t)skip / BLIST_META_RADIX);

#if 0
	printf("FREE (%04lx,%lu) FROM (%04lx,%lu)\n",
	    freeBlk, count,
	    blk, radix
	);
#endif

	/*
	 * ALL-ALLOCATED special case, initialize for recursion.
	 *
	 * We will short-cut the ALL-ALLOCATED -> ALL-FREE case.
	 */
	if (scan->u.bmu_avail == 0) {
		scan->u.bmu_avail = count;
		scan->bm_bighint = count;

		if (count != radix)  {
			for (i = 1; i <= skip; i += next_skip) {
				if (scan[i].bm_bighint == (swblk_t)-1)
					break;
				scan[i].bm_bighint = 0;
				if (next_skip == 1) {
					scan[i].u.bmu_bitmap = 0;
				} else {
					scan[i].u.bmu_avail = 0;
				}
			}
			/* fall through */
		}
	} else {
		scan->u.bmu_avail += count;
		/* scan->bm_bighint = radix; */
	}

	/*
	 * ALL-FREE special case.
	 *
	 * Set bighint for higher levels to snoop.
	 */
	if (scan->u.bmu_avail == radix) {
		scan->bm_bighint = radix;
		return;
	}

	/*
	 * Break the free down into its components
	 */
	if (scan->u.bmu_avail > radix) {
		panic("%s: freeing already "
		      "free blocks (%lu) %lu/%lu",
		      __func__, count, (long)scan->u.bmu_avail, radix);
	}

	radix /= BLIST_META_RADIX;

	i = (freeBlk - blk) / (swblk_t)radix;
	blk += i * (swblk_t)radix;
	i = i * next_skip + 1;

	while (i <= skip && blk < freeBlk + count) {
		swblk_t v;

		v = blk + (swblk_t)radix - freeBlk;
		if (v > count)
			v = count;

		if (scan->bm_bighint == (swblk_t)-1)
			panic("%s: freeing unexpected range", __func__);

		if (next_skip == 1) {
			blst_leaf_free(&scan[i], freeBlk, v);
		} else {
			blst_meta_free(&scan[i], freeBlk, v,
				       radix, next_skip - 1, blk);
		}

		/*
		 * After having dealt with the becomes-all-free case any
		 * partial free will not be able to bring us to the
		 * becomes-all-free state.
		 *
		 * We can raise bighint to at least the sub-segment's
		 * bighint.
		 */
		if (scan->bm_bighint < scan[i].bm_bighint) {
		    scan->bm_bighint = scan[i].bm_bighint;
		}
		count -= v;
		freeBlk += v;
		blk += (swblk_t)radix;
		i += next_skip;
	}
}

/*
 * BLST_LEAF_FILL() -	allocate specific blocks in leaf bitmap
 *
 *	Allocates all blocks in the specified range regardless of
 *	any existing allocations in that range.  Returns the number
 *	of blocks allocated by the call.
 */
static swblk_t
blst_leaf_fill(blmeta_t *scan, swblk_t blk, swblk_t count)
{
	int n = blk & (BLIST_BMAP_RADIX - 1);
	swblk_t nblks;
	u_swblk_t mask, bitmap;

	mask = ((u_swblk_t)-1 << n) &
	    ((u_swblk_t)-1 >> (BLIST_BMAP_RADIX - count - n));

	/* Count the number of blocks we're about to allocate */
	bitmap = scan->u.bmu_bitmap & mask;
	for (nblks = 0; bitmap != 0; nblks++)
		bitmap &= bitmap - 1;

	scan->u.bmu_bitmap &= ~mask;
	return (nblks);
}

/*
 * BLST_META_FILL() -	allocate specific blocks at a meta node
 *
 *	Allocates the specified range of blocks, regardless of
 *	any existing allocations in the range.  The range must
 *	be within the extent of this node.  Returns the number
 *	of blocks allocated by the call.
 */
static swblk_t
blst_meta_fill(blmeta_t *scan, swblk_t fillBlk, swblk_t count,
	       swblk_t radix, swblk_t skip, swblk_t blk)
{
	swblk_t i;
	swblk_t next_skip = ((swblk_t)skip / BLIST_META_RADIX);
	swblk_t nblks = 0;

	if (count == radix || scan->u.bmu_avail == 0) {
		/*
		 * ALL-ALLOCATED special case
		 */
		nblks = scan->u.bmu_avail;
		scan->u.bmu_avail = 0;
		scan->bm_bighint = count;
		return (nblks);
	}

	if (scan->u.bmu_avail == radix) {
		radix /= BLIST_META_RADIX;

		/*
		 * ALL-FREE special case, initialize sublevel
		 */
		for (i = 1; i <= skip; i += next_skip) {
			if (scan[i].bm_bighint == (swblk_t)-1)
				break;
			if (next_skip == 1) {
				scan[i].u.bmu_bitmap = (u_swblk_t)-1;
				scan[i].bm_bighint = BLIST_BMAP_RADIX;
			} else {
				scan[i].bm_bighint = (swblk_t)radix;
				scan[i].u.bmu_avail = (swblk_t)radix;
			}
		}
	} else {
		radix /= BLIST_META_RADIX;
	}

	if (count > (swblk_t)radix)
		panic("%s: allocation too large", __func__);

	i = (fillBlk - blk) / (swblk_t)radix;
	blk += i * (swblk_t)radix;
	i = i * next_skip + 1;

	while (i <= skip && blk < fillBlk + count) {
		swblk_t v;

		v = blk + (swblk_t)radix - fillBlk;
		if (v > count)
			v = count;

		if (scan->bm_bighint == (swblk_t)-1)
			panic("%s: filling unexpected range", __func__);

		if (next_skip == 1) {
			nblks += blst_leaf_fill(&scan[i], fillBlk, v);
		} else {
			nblks += blst_meta_fill(&scan[i], fillBlk, v,
			    radix, next_skip - 1, blk);
		}
		count -= v;
		fillBlk += v;
		blk += (swblk_t)radix;
		i += next_skip;
	}
	scan->u.bmu_avail -= nblks;
	return (nblks);
}

/*
 * BLIST_RADIX_COPY() - copy one radix tree to another
 *
 *	Locates free space in the source tree and frees it in the destination
 *	tree.  The space may not already be free in the destination.
 */

static void
blst_copy(blmeta_t *scan, swblk_t blk, swblk_t radix,
	  swblk_t skip, blist_t dest, swblk_t count)
{
	swblk_t next_skip;
	swblk_t i;

	/*
	 * Leaf node
	 */

	if (radix == BLIST_BMAP_RADIX) {
		u_swblk_t v = scan->u.bmu_bitmap;

		if (v == (u_swblk_t)-1) {
			blist_free(dest, blk, count);
		} else if (v != 0) {
			for (i = 0; i < BLIST_BMAP_RADIX && i < count; ++i) {
				if (v & ((swblk_t)1 << i))
					blist_free(dest, blk + i, 1);
			}
		}
		return;
	}

	/*
	 * Meta node
	 */

	if (scan->u.bmu_avail == 0) {
		/*
		 * Source all allocated, leave dest allocated
		 */
		return;
	}
	if (scan->u.bmu_avail == radix) {
		/*
		 * Source all free, free entire dest
		 */
		if (count < radix)
			blist_free(dest, blk, count);
		else
			blist_free(dest, blk, (swblk_t)radix);
		return;
	}


	radix /= BLIST_META_RADIX;
	next_skip = ((u_swblk_t)skip / BLIST_META_RADIX);

	for (i = 1; count && i <= skip; i += next_skip) {
		if (scan[i].bm_bighint == (swblk_t)-1)
			break;

		if (count >= (swblk_t)radix) {
			blst_copy(
			    &scan[i],
			    blk,
			    radix,
			    next_skip - 1,
			    dest,
			    (swblk_t)radix
			);
			count -= (swblk_t)radix;
		} else {
			if (count) {
				blst_copy(
				    &scan[i],
				    blk,
				    radix,
				    next_skip - 1,
				    dest,
				    count
				);
			}
			count = 0;
		}
		blk += (swblk_t)radix;
	}
}

/*
 * BLST_RADIX_INIT() - initialize radix tree
 *
 *	Initialize our meta structures and bitmaps and calculate the exact
 *	amount of space required to manage 'count' blocks - this space may
 *	be considerably less than the calculated radix due to the large
 *	RADIX values we use.
 */

static swblk_t
blst_radix_init(blmeta_t *scan, swblk_t radix, swblk_t skip, swblk_t count)
{
	swblk_t i;
	swblk_t next_skip;
	swblk_t memindex = 0;

	/*
	 * Leaf node
	 */

	if (radix == BLIST_BMAP_RADIX) {
		if (scan) {
			scan->bm_bighint = 0;
			scan->u.bmu_bitmap = 0;
		}
		return(memindex);
	}

	/*
	 * Meta node.  If allocating the entire object we can special
	 * case it.  However, we need to figure out how much memory
	 * is required to manage 'count' blocks, so we continue on anyway.
	 */

	if (scan) {
		scan->bm_bighint = 0;
		scan->u.bmu_avail = 0;
	}

	radix /= BLIST_META_RADIX;
	next_skip = ((u_swblk_t)skip / BLIST_META_RADIX);

	for (i = 1; i <= skip; i += next_skip) {
		if (count >= (swblk_t)radix) {
			/*
			 * Allocate the entire object
			 */
			memindex = i + blst_radix_init(
			    ((scan) ? &scan[i] : NULL),
			    radix,
			    next_skip - 1,
			    (swblk_t)radix
			);
			count -= (swblk_t)radix;
		} else if (count > 0) {
			/*
			 * Allocate a partial object
			 */
			memindex = i + blst_radix_init(
			    ((scan) ? &scan[i] : NULL),
			    radix,
			    next_skip - 1,
			    count
			);
			count = 0;
		} else {
			/*
			 * Add terminator and break out
			 */
			if (scan)
				scan[i].bm_bighint = (swblk_t)-1;
			break;
		}
	}
	if (memindex < i)
		memindex = i;
	return(memindex);
}

#if defined(BLIST_DEBUG) || defined(DDB)

static void
blst_radix_print(blmeta_t *scan, swblk_t blk, swblk_t radix, swblk_t skip, int tab)
{
	swblk_t i;
	swblk_t next_skip;

	if (radix == BLIST_BMAP_RADIX) {
		printf(
		    "%*.*s(%04lx,%lu): bitmap %0*llx big=%lu\n",
		    tab, tab, "",
		    blk, radix,
		    (int)(1 + (BLIST_BMAP_RADIX - 1) / 4),
		    scan->u.bmu_bitmap,
		    scan->bm_bighint
		);
		return;
	}

	if (scan->u.bmu_avail == 0) {
		printf(
		    "%*.*s(%04lx,%lu) ALL ALLOCATED\n",
		    tab, tab, "",
		    blk,
		    radix
		);
		return;
	}
	if (scan->u.bmu_avail == radix) {
		printf(
		    "%*.*s(%04lx,%lu) ALL FREE\n",
		    tab, tab, "",
		    blk,
		    radix
		);
		return;
	}

	printf(
	    "%*.*s(%04lx,%lu): subtree (%lu/%lu) big=%lu {\n",
	    tab, tab, "",
	    blk, radix,
	    scan->u.bmu_avail,
	    radix,
	    scan->bm_bighint
	);

	radix /= BLIST_META_RADIX;
	next_skip = ((u_swblk_t)skip / BLIST_META_RADIX);
	tab += 4;

	for (i = 1; i <= skip; i += next_skip) {
		if (scan[i].bm_bighint == (swblk_t)-1) {
			printf(
			    "%*.*s(%04lx,%lu): Terminator\n",
			    tab, tab, "",
			    blk, radix
			);
			break;
		}
		blst_radix_print(
		    &scan[i],
		    blk,
		    radix,
		    next_skip - 1,
		    tab
		);
		blk += (swblk_t)radix;
	}
	tab -= 4;

	printf(
	    "%*.*s}\n",
	    tab, tab, ""
	);
}

#endif

#if !defined(_KERNEL) && defined(BLIST_DEBUG)

int
main(int ac, char **av)
{
	swblk_t size = 1024;
	swblk_t i;
	blist_t bl;

	for (i = 1; i < (swblk_t)ac; ++i) {
		const char *ptr = av[i];
		if (*ptr != '-') {
			size = strtol(ptr, NULL, 0);
			continue;
		}
		ptr += 2;
		fprintf(stderr, "Bad option: %s\n", ptr - 2);
		exit(1);
	}
	bl = blist_create(size);
	blist_free(bl, 0, size);

	for (;;) {
		char buf[1024];
		swblk_t da = 0;
		swblk_t count = 0;
		swblk_t blkat;


		printf("%lu/%lu/%lu> ",
			bl->bl_free, size, bl->bl_radix);
		fflush(stdout);
		if (fgets(buf, sizeof(buf), stdin) == NULL)
			break;
		switch(buf[0]) {
		case '#':
			continue;
		case 'r':
			if (sscanf(buf + 1, "%li", &count) == 1) {
				blist_resize(&bl, count, 1);
				size = count;
			} else {
				printf("?\n");
			}
		case 'p':
			blist_print(bl);
			break;
		case 'a':
			if (sscanf(buf + 1, "%li %li", &count, &blkat) == 1) {
				printf("count %lu\n", count);
				swblk_t blk = blist_alloc(bl, count);
				if (blk == SWAPBLK_NONE)
					printf("    R=SWAPBLK_NONE\n");
				else
					printf("    R=%04lx\n", blk);
			} else if (sscanf(buf + 1, "%li %li", &count, &blkat) == 2) {
				swblk_t blk = blist_allocat(bl, count, blkat);
				if (blk == SWAPBLK_NONE)
					printf("    R=SWAPBLK_NONE\n");
				else
					printf("    R=%04lx\n", blk);
			} else {
				printf("?\n");
			}
			break;
		case 'f':
			if (sscanf(buf + 1, "%li %li", &da, &count) == 2) {
				blist_free(bl, da, count);
			} else {
				printf("?\n");
			}
			break;
		case 'g': {
				swblk_t b, e;
				blist_gapfind(bl, &b, &e);
				printf("gapfind: begin=%04lx end=%04lx size=%lu\n",
				    b, e, e-b);
				break;
			}
		case 'l':
			if (sscanf(buf + 1, "%li %li", &da, &count) == 2) {
				printf("    n=%lu\n",
				    blist_fill(bl, da, count));
			} else {
				printf("?\n");
			}
			break;
		case '?':
		case 'h':
			puts(
			    "p          -print\n"
			    "a %li      -allocate\n"
			    "f %li %li  -free\n"
			    "l %li %li	-fill\n"
			    "g		-gapfind\n"
			    "r %li      -resize\n"
			    "h/?        -help\n"
			    "    hex may be specified with 0x prefix\n"
			);
			break;
		default:
			printf("?\n");
			break;
		}
	}
	return(0);
}

#endif
