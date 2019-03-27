/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 1998 Matthew Dillon.  All Rights Reserved.
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
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS
 * OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE
 * GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
 * NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

/*
 * Implements bitmap resource lists.
 *
 *	Usage:
 *		blist = blist_create(blocks, flags)
 *		(void)  blist_destroy(blist)
 *		blkno = blist_alloc(blist, count)
 *		(void)  blist_free(blist, blkno, count)
 *		nblks = blist_fill(blist, blkno, count)
 *		(void)  blist_resize(&blist, count, freeextra, flags)
 *		
 *
 *	Notes:
 *		on creation, the entire list is marked reserved.  You should
 *		first blist_free() the sections you want to make available
 *		for allocation before doing general blist_alloc()/free()
 *		ops.
 *
 *		SWAPBLK_NONE is returned on failure.  This module is typically
 *		capable of managing up to (2^63) blocks per blist, though
 *		the memory utilization would be insane if you actually did
 *		that.  Managing something like 512MB worth of 4K blocks 
 *		eats around 32 KBytes of memory. 
 *
 * $FreeBSD$

 */

#ifndef _SYS_BLIST_H_
#define _SYS_BLIST_H_

typedef	uint64_t	u_daddr_t;	/* unsigned disk address */

/*
 * note: currently use SWAPBLK_NONE as an absolute value rather then 
 * a flag bit.
 */

#define SWAPBLK_MASK	((daddr_t)((u_daddr_t)-1 >> 1))		/* mask */
#define SWAPBLK_NONE	((daddr_t)((u_daddr_t)SWAPBLK_MASK + 1))/* flag */

/*
 * Both blmeta and bmu_bitmap MUST be a power of 2 in size.
 */

typedef struct blmeta {
	u_daddr_t	bm_bitmap;	/* bitmap if we are a leaf	*/
	daddr_t		bm_bighint;	/* biggest contiguous block hint*/
} blmeta_t;

typedef struct blist {
	daddr_t		bl_blocks;	/* area of coverage		*/
	daddr_t		bl_avail;	/* # available blocks */
	u_daddr_t	bl_radix;	/* coverage radix		*/
	daddr_t		bl_cursor;	/* next-fit search starts at	*/
	blmeta_t	bl_root[1];	/* root of radix tree		*/
} *blist_t;

#define BLIST_BMAP_RADIX	(sizeof(u_daddr_t)*8)
#define BLIST_META_RADIX	BLIST_BMAP_RADIX

#define BLIST_MAX_ALLOC		BLIST_BMAP_RADIX

struct sbuf;

daddr_t	blist_alloc(blist_t blist, daddr_t count);
daddr_t	blist_avail(blist_t blist);
blist_t	blist_create(daddr_t blocks, int flags);
void	blist_destroy(blist_t blist);
daddr_t	blist_fill(blist_t bl, daddr_t blkno, daddr_t count);
void	blist_free(blist_t blist, daddr_t blkno, daddr_t count);
void	blist_print(blist_t blist);
void	blist_resize(blist_t *pblist, daddr_t count, int freenew, int flags);
void	blist_stats(blist_t blist, struct sbuf *s);

#endif	/* _SYS_BLIST_H_ */

