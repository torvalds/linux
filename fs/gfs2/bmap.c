/*
 * Copyright (C) Sistina Software, Inc.  1997-2003 All rights reserved.
 * Copyright (C) 2004-2006 Red Hat, Inc.  All rights reserved.
 *
 * This copyrighted material is made available to anyone wishing to use,
 * modify, copy, or redistribute it subject to the terms and conditions
 * of the GNU General Public License version 2.
 */

#include <linux/spinlock.h>
#include <linux/completion.h>
#include <linux/buffer_head.h>
#include <linux/blkdev.h>
#include <linux/gfs2_ondisk.h>
#include <linux/crc32.h>
#include <linux/iomap.h>

#include "gfs2.h"
#include "incore.h"
#include "bmap.h"
#include "glock.h"
#include "inode.h"
#include "meta_io.h"
#include "quota.h"
#include "rgrp.h"
#include "log.h"
#include "super.h"
#include "trans.h"
#include "dir.h"
#include "util.h"
#include "trace_gfs2.h"

/* This doesn't need to be that large as max 64 bit pointers in a 4k
 * block is 512, so __u16 is fine for that. It saves stack space to
 * keep it small.
 */
struct metapath {
	struct buffer_head *mp_bh[GFS2_MAX_META_HEIGHT];
	__u16 mp_list[GFS2_MAX_META_HEIGHT];
	int mp_fheight; /* find_metapath height */
	int mp_aheight; /* actual height (lookup height) */
};

/**
 * gfs2_unstuffer_page - unstuff a stuffed inode into a block cached by a page
 * @ip: the inode
 * @dibh: the dinode buffer
 * @block: the block number that was allocated
 * @page: The (optional) page. This is looked up if @page is NULL
 *
 * Returns: errno
 */

static int gfs2_unstuffer_page(struct gfs2_inode *ip, struct buffer_head *dibh,
			       u64 block, struct page *page)
{
	struct inode *inode = &ip->i_inode;
	struct buffer_head *bh;
	int release = 0;

	if (!page || page->index) {
		page = find_or_create_page(inode->i_mapping, 0, GFP_NOFS);
		if (!page)
			return -ENOMEM;
		release = 1;
	}

	if (!PageUptodate(page)) {
		void *kaddr = kmap(page);
		u64 dsize = i_size_read(inode);
 
		if (dsize > gfs2_max_stuffed_size(ip))
			dsize = gfs2_max_stuffed_size(ip);

		memcpy(kaddr, dibh->b_data + sizeof(struct gfs2_dinode), dsize);
		memset(kaddr + dsize, 0, PAGE_SIZE - dsize);
		kunmap(page);

		SetPageUptodate(page);
	}

	if (!page_has_buffers(page))
		create_empty_buffers(page, BIT(inode->i_blkbits),
				     BIT(BH_Uptodate));

	bh = page_buffers(page);

	if (!buffer_mapped(bh))
		map_bh(bh, inode->i_sb, block);

	set_buffer_uptodate(bh);
	if (!gfs2_is_jdata(ip))
		mark_buffer_dirty(bh);
	if (!gfs2_is_writeback(ip))
		gfs2_trans_add_data(ip->i_gl, bh);

	if (release) {
		unlock_page(page);
		put_page(page);
	}

	return 0;
}

/**
 * gfs2_unstuff_dinode - Unstuff a dinode when the data has grown too big
 * @ip: The GFS2 inode to unstuff
 * @page: The (optional) page. This is looked up if the @page is NULL
 *
 * This routine unstuffs a dinode and returns it to a "normal" state such
 * that the height can be grown in the traditional way.
 *
 * Returns: errno
 */

int gfs2_unstuff_dinode(struct gfs2_inode *ip, struct page *page)
{
	struct buffer_head *bh, *dibh;
	struct gfs2_dinode *di;
	u64 block = 0;
	int isdir = gfs2_is_dir(ip);
	int error;

	down_write(&ip->i_rw_mutex);

	error = gfs2_meta_inode_buffer(ip, &dibh);
	if (error)
		goto out;

	if (i_size_read(&ip->i_inode)) {
		/* Get a free block, fill it with the stuffed data,
		   and write it out to disk */

		unsigned int n = 1;
		error = gfs2_alloc_blocks(ip, &block, &n, 0, NULL);
		if (error)
			goto out_brelse;
		if (isdir) {
			gfs2_trans_add_unrevoke(GFS2_SB(&ip->i_inode), block, 1);
			error = gfs2_dir_get_new_buffer(ip, block, &bh);
			if (error)
				goto out_brelse;
			gfs2_buffer_copy_tail(bh, sizeof(struct gfs2_meta_header),
					      dibh, sizeof(struct gfs2_dinode));
			brelse(bh);
		} else {
			error = gfs2_unstuffer_page(ip, dibh, block, page);
			if (error)
				goto out_brelse;
		}
	}

	/*  Set up the pointer to the new block  */

	gfs2_trans_add_meta(ip->i_gl, dibh);
	di = (struct gfs2_dinode *)dibh->b_data;
	gfs2_buffer_clear_tail(dibh, sizeof(struct gfs2_dinode));

	if (i_size_read(&ip->i_inode)) {
		*(__be64 *)(di + 1) = cpu_to_be64(block);
		gfs2_add_inode_blocks(&ip->i_inode, 1);
		di->di_blocks = cpu_to_be64(gfs2_get_inode_blocks(&ip->i_inode));
	}

	ip->i_height = 1;
	di->di_height = cpu_to_be16(1);

out_brelse:
	brelse(dibh);
out:
	up_write(&ip->i_rw_mutex);
	return error;
}


/**
 * find_metapath - Find path through the metadata tree
 * @sdp: The superblock
 * @mp: The metapath to return the result in
 * @block: The disk block to look up
 * @height: The pre-calculated height of the metadata tree
 *
 *   This routine returns a struct metapath structure that defines a path
 *   through the metadata of inode "ip" to get to block "block".
 *
 *   Example:
 *   Given:  "ip" is a height 3 file, "offset" is 101342453, and this is a
 *   filesystem with a blocksize of 4096.
 *
 *   find_metapath() would return a struct metapath structure set to:
 *   mp_offset = 101342453, mp_height = 3, mp_list[0] = 0, mp_list[1] = 48,
 *   and mp_list[2] = 165.
 *
 *   That means that in order to get to the block containing the byte at
 *   offset 101342453, we would load the indirect block pointed to by pointer
 *   0 in the dinode.  We would then load the indirect block pointed to by
 *   pointer 48 in that indirect block.  We would then load the data block
 *   pointed to by pointer 165 in that indirect block.
 *
 *             ----------------------------------------
 *             | Dinode |                             |
 *             |        |                            4|
 *             |        |0 1 2 3 4 5                 9|
 *             |        |                            6|
 *             ----------------------------------------
 *                       |
 *                       |
 *                       V
 *             ----------------------------------------
 *             | Indirect Block                       |
 *             |                                     5|
 *             |            4 4 4 4 4 5 5            1|
 *             |0           5 6 7 8 9 0 1            2|
 *             ----------------------------------------
 *                                |
 *                                |
 *                                V
 *             ----------------------------------------
 *             | Indirect Block                       |
 *             |                         1 1 1 1 1   5|
 *             |                         6 6 6 6 6   1|
 *             |0                        3 4 5 6 7   2|
 *             ----------------------------------------
 *                                           |
 *                                           |
 *                                           V
 *             ----------------------------------------
 *             | Data block containing offset         |
 *             |            101342453                 |
 *             |                                      |
 *             |                                      |
 *             ----------------------------------------
 *
 */

static void find_metapath(const struct gfs2_sbd *sdp, u64 block,
			  struct metapath *mp, unsigned int height)
{
	unsigned int i;

	mp->mp_fheight = height;
	for (i = height; i--;)
		mp->mp_list[i] = do_div(block, sdp->sd_inptrs);
}

static inline unsigned int metapath_branch_start(const struct metapath *mp)
{
	if (mp->mp_list[0] == 0)
		return 2;
	return 1;
}

/**
 * metaptr1 - Return the first possible metadata pointer in a metapath buffer
 * @height: The metadata height (0 = dinode)
 * @mp: The metapath
 */
static inline __be64 *metaptr1(unsigned int height, const struct metapath *mp)
{
	struct buffer_head *bh = mp->mp_bh[height];
	if (height == 0)
		return ((__be64 *)(bh->b_data + sizeof(struct gfs2_dinode)));
	return ((__be64 *)(bh->b_data + sizeof(struct gfs2_meta_header)));
}

/**
 * metapointer - Return pointer to start of metadata in a buffer
 * @height: The metadata height (0 = dinode)
 * @mp: The metapath
 *
 * Return a pointer to the block number of the next height of the metadata
 * tree given a buffer containing the pointer to the current height of the
 * metadata tree.
 */

static inline __be64 *metapointer(unsigned int height, const struct metapath *mp)
{
	__be64 *p = metaptr1(height, mp);
	return p + mp->mp_list[height];
}

static void gfs2_metapath_ra(struct gfs2_glock *gl, __be64 *start, __be64 *end)
{
	const __be64 *t;

	for (t = start; t < end; t++) {
		struct buffer_head *rabh;

		if (!*t)
			continue;

		rabh = gfs2_getbuf(gl, be64_to_cpu(*t), CREATE);
		if (trylock_buffer(rabh)) {
			if (!buffer_uptodate(rabh)) {
				rabh->b_end_io = end_buffer_read_sync;
				submit_bh(REQ_OP_READ,
					  REQ_RAHEAD | REQ_META | REQ_PRIO,
					  rabh);
				continue;
			}
			unlock_buffer(rabh);
		}
		brelse(rabh);
	}
}

static int __fillup_metapath(struct gfs2_inode *ip, struct metapath *mp,
			     unsigned int x, unsigned int h)
{
	for (; x < h; x++) {
		__be64 *ptr = metapointer(x, mp);
		u64 dblock = be64_to_cpu(*ptr);
		int ret;

		if (!dblock)
			break;
		ret = gfs2_meta_indirect_buffer(ip, x + 1, dblock, &mp->mp_bh[x + 1]);
		if (ret)
			return ret;
	}
	mp->mp_aheight = x + 1;
	return 0;
}

/**
 * lookup_metapath - Walk the metadata tree to a specific point
 * @ip: The inode
 * @mp: The metapath
 *
 * Assumes that the inode's buffer has already been looked up and
 * hooked onto mp->mp_bh[0] and that the metapath has been initialised
 * by find_metapath().
 *
 * If this function encounters part of the tree which has not been
 * allocated, it returns the current height of the tree at the point
 * at which it found the unallocated block. Blocks which are found are
 * added to the mp->mp_bh[] list.
 *
 * Returns: error
 */

static int lookup_metapath(struct gfs2_inode *ip, struct metapath *mp)
{
	return __fillup_metapath(ip, mp, 0, ip->i_height - 1);
}

/**
 * fillup_metapath - fill up buffers for the metadata path to a specific height
 * @ip: The inode
 * @mp: The metapath
 * @h: The height to which it should be mapped
 *
 * Similar to lookup_metapath, but does lookups for a range of heights
 *
 * Returns: error or the number of buffers filled
 */

static int fillup_metapath(struct gfs2_inode *ip, struct metapath *mp, int h)
{
	unsigned int x = 0;
	int ret;

	if (h) {
		/* find the first buffer we need to look up. */
		for (x = h - 1; x > 0; x--) {
			if (mp->mp_bh[x])
				break;
		}
	}
	ret = __fillup_metapath(ip, mp, x, h);
	if (ret)
		return ret;
	return mp->mp_aheight - x - 1;
}

static inline void release_metapath(struct metapath *mp)
{
	int i;

	for (i = 0; i < GFS2_MAX_META_HEIGHT; i++) {
		if (mp->mp_bh[i] == NULL)
			break;
		brelse(mp->mp_bh[i]);
	}
}

/**
 * gfs2_extent_length - Returns length of an extent of blocks
 * @start: Start of the buffer
 * @len: Length of the buffer in bytes
 * @ptr: Current position in the buffer
 * @limit: Max extent length to return (0 = unlimited)
 * @eob: Set to 1 if we hit "end of block"
 *
 * If the first block is zero (unallocated) it will return the number of
 * unallocated blocks in the extent, otherwise it will return the number
 * of contiguous blocks in the extent.
 *
 * Returns: The length of the extent (minimum of one block)
 */

static inline unsigned int gfs2_extent_length(void *start, unsigned int len, __be64 *ptr, size_t limit, int *eob)
{
	const __be64 *end = (start + len);
	const __be64 *first = ptr;
	u64 d = be64_to_cpu(*ptr);

	*eob = 0;
	do {
		ptr++;
		if (ptr >= end)
			break;
		if (limit && --limit == 0)
			break;
		if (d)
			d++;
	} while(be64_to_cpu(*ptr) == d);
	if (ptr >= end)
		*eob = 1;
	return (ptr - first);
}

static inline void bmap_lock(struct gfs2_inode *ip, int create)
{
	if (create)
		down_write(&ip->i_rw_mutex);
	else
		down_read(&ip->i_rw_mutex);
}

static inline void bmap_unlock(struct gfs2_inode *ip, int create)
{
	if (create)
		up_write(&ip->i_rw_mutex);
	else
		up_read(&ip->i_rw_mutex);
}

static inline __be64 *gfs2_indirect_init(struct metapath *mp,
					 struct gfs2_glock *gl, unsigned int i,
					 unsigned offset, u64 bn)
{
	__be64 *ptr = (__be64 *)(mp->mp_bh[i - 1]->b_data +
		       ((i > 1) ? sizeof(struct gfs2_meta_header) :
				 sizeof(struct gfs2_dinode)));
	BUG_ON(i < 1);
	BUG_ON(mp->mp_bh[i] != NULL);
	mp->mp_bh[i] = gfs2_meta_new(gl, bn);
	gfs2_trans_add_meta(gl, mp->mp_bh[i]);
	gfs2_metatype_set(mp->mp_bh[i], GFS2_METATYPE_IN, GFS2_FORMAT_IN);
	gfs2_buffer_clear_tail(mp->mp_bh[i], sizeof(struct gfs2_meta_header));
	ptr += offset;
	*ptr = cpu_to_be64(bn);
	return ptr;
}

enum alloc_state {
	ALLOC_DATA = 0,
	ALLOC_GROW_DEPTH = 1,
	ALLOC_GROW_HEIGHT = 2,
	/* ALLOC_UNSTUFF = 3,   TBD and rather complicated */
};

/**
 * gfs2_bmap_alloc - Build a metadata tree of the requested height
 * @inode: The GFS2 inode
 * @lblock: The logical starting block of the extent
 * @bh_map: This is used to return the mapping details
 * @zero_new: True if newly allocated blocks should be zeroed
 * @mp: The metapath, with proper height information calculated
 * @maxlen: The max number of data blocks to alloc
 * @dblock: Pointer to return the resulting new block
 * @dblks: Pointer to return the number of blocks allocated
 *
 * In this routine we may have to alloc:
 *   i) Indirect blocks to grow the metadata tree height
 *  ii) Indirect blocks to fill in lower part of the metadata tree
 * iii) Data blocks
 *
 * The function is in two parts. The first part works out the total
 * number of blocks which we need. The second part does the actual
 * allocation asking for an extent at a time (if enough contiguous free
 * blocks are available, there will only be one request per bmap call)
 * and uses the state machine to initialise the blocks in order.
 *
 * Returns: errno on error
 */

static int gfs2_iomap_alloc(struct inode *inode, struct iomap *iomap,
			    unsigned flags, struct metapath *mp)
{
	struct gfs2_inode *ip = GFS2_I(inode);
	struct gfs2_sbd *sdp = GFS2_SB(inode);
	struct buffer_head *dibh = mp->mp_bh[0];
	u64 bn;
	unsigned n, i, blks, alloced = 0, iblks = 0, branch_start = 0;
	unsigned dblks = 0;
	unsigned ptrs_per_blk;
	const unsigned end_of_metadata = mp->mp_fheight - 1;
	enum alloc_state state;
	__be64 *ptr;
	__be64 zero_bn = 0;
	size_t maxlen = iomap->length >> inode->i_blkbits;

	BUG_ON(mp->mp_aheight < 1);
	BUG_ON(dibh == NULL);

	gfs2_trans_add_meta(ip->i_gl, dibh);

	if (mp->mp_fheight == mp->mp_aheight) {
		struct buffer_head *bh;
		int eob;

		/* Bottom indirect block exists, find unalloced extent size */
		ptr = metapointer(end_of_metadata, mp);
		bh = mp->mp_bh[end_of_metadata];
		dblks = gfs2_extent_length(bh->b_data, bh->b_size, ptr,
					   maxlen, &eob);
		BUG_ON(dblks < 1);
		state = ALLOC_DATA;
	} else {
		/* Need to allocate indirect blocks */
		ptrs_per_blk = mp->mp_fheight > 1 ? sdp->sd_inptrs :
			sdp->sd_diptrs;
		dblks = min(maxlen, (size_t)(ptrs_per_blk -
					     mp->mp_list[end_of_metadata]));
		if (mp->mp_fheight == ip->i_height) {
			/* Writing into existing tree, extend tree down */
			iblks = mp->mp_fheight - mp->mp_aheight;
			state = ALLOC_GROW_DEPTH;
		} else {
			/* Building up tree height */
			state = ALLOC_GROW_HEIGHT;
			iblks = mp->mp_fheight - ip->i_height;
			branch_start = metapath_branch_start(mp);
			iblks += (mp->mp_fheight - branch_start);
		}
	}

	/* start of the second part of the function (state machine) */

	blks = dblks + iblks;
	i = mp->mp_aheight;
	do {
		int error;
		n = blks - alloced;
		error = gfs2_alloc_blocks(ip, &bn, &n, 0, NULL);
		if (error)
			return error;
		alloced += n;
		if (state != ALLOC_DATA || gfs2_is_jdata(ip))
			gfs2_trans_add_unrevoke(sdp, bn, n);
		switch (state) {
		/* Growing height of tree */
		case ALLOC_GROW_HEIGHT:
			if (i == 1) {
				ptr = (__be64 *)(dibh->b_data +
						 sizeof(struct gfs2_dinode));
				zero_bn = *ptr;
			}
			for (; i - 1 < mp->mp_fheight - ip->i_height && n > 0;
			     i++, n--)
				gfs2_indirect_init(mp, ip->i_gl, i, 0, bn++);
			if (i - 1 == mp->mp_fheight - ip->i_height) {
				i--;
				gfs2_buffer_copy_tail(mp->mp_bh[i],
						sizeof(struct gfs2_meta_header),
						dibh, sizeof(struct gfs2_dinode));
				gfs2_buffer_clear_tail(dibh,
						sizeof(struct gfs2_dinode) +
						sizeof(__be64));
				ptr = (__be64 *)(mp->mp_bh[i]->b_data +
					sizeof(struct gfs2_meta_header));
				*ptr = zero_bn;
				state = ALLOC_GROW_DEPTH;
				for(i = branch_start; i < mp->mp_fheight; i++) {
					if (mp->mp_bh[i] == NULL)
						break;
					brelse(mp->mp_bh[i]);
					mp->mp_bh[i] = NULL;
				}
				i = branch_start;
			}
			if (n == 0)
				break;
		/* Branching from existing tree */
		case ALLOC_GROW_DEPTH:
			if (i > 1 && i < mp->mp_fheight)
				gfs2_trans_add_meta(ip->i_gl, mp->mp_bh[i-1]);
			for (; i < mp->mp_fheight && n > 0; i++, n--)
				gfs2_indirect_init(mp, ip->i_gl, i,
						   mp->mp_list[i-1], bn++);
			if (i == mp->mp_fheight)
				state = ALLOC_DATA;
			if (n == 0)
				break;
		/* Tree complete, adding data blocks */
		case ALLOC_DATA:
			BUG_ON(n > dblks);
			BUG_ON(mp->mp_bh[end_of_metadata] == NULL);
			gfs2_trans_add_meta(ip->i_gl, mp->mp_bh[end_of_metadata]);
			dblks = n;
			ptr = metapointer(end_of_metadata, mp);
			iomap->addr = bn << inode->i_blkbits;
			iomap->flags |= IOMAP_F_NEW;
			while (n-- > 0)
				*ptr++ = cpu_to_be64(bn++);
			break;
		}
	} while (iomap->addr == IOMAP_NULL_ADDR);

	iomap->length = (u64)dblks << inode->i_blkbits;
	ip->i_height = mp->mp_fheight;
	gfs2_add_inode_blocks(&ip->i_inode, alloced);
	gfs2_dinode_out(ip, mp->mp_bh[0]->b_data);
	return 0;
}

/**
 * hole_size - figure out the size of a hole
 * @inode: The inode
 * @lblock: The logical starting block number
 * @mp: The metapath
 *
 * Returns: The hole size in bytes
 *
 */
static u64 hole_size(struct inode *inode, sector_t lblock, struct metapath *mp)
{
	struct gfs2_inode *ip = GFS2_I(inode);
	struct gfs2_sbd *sdp = GFS2_SB(inode);
	struct metapath mp_eof;
	u64 factor = 1;
	int hgt;
	u64 holesz = 0;
	const __be64 *first, *end, *ptr;
	const struct buffer_head *bh;
	u64 lblock_stop = (i_size_read(inode) - 1) >> inode->i_blkbits;
	int zeroptrs;
	bool done = false;

	/* Get another metapath, to the very last byte */
	find_metapath(sdp, lblock_stop, &mp_eof, ip->i_height);
	for (hgt = ip->i_height - 1; hgt >= 0 && !done; hgt--) {
		bh = mp->mp_bh[hgt];
		if (bh) {
			zeroptrs = 0;
			first = metapointer(hgt, mp);
			end = (const __be64 *)(bh->b_data + bh->b_size);

			for (ptr = first; ptr < end; ptr++) {
				if (*ptr) {
					done = true;
					break;
				} else {
					zeroptrs++;
				}
			}
		} else {
			zeroptrs = sdp->sd_inptrs;
		}
		if (factor * zeroptrs >= lblock_stop - lblock + 1) {
			holesz = lblock_stop - lblock + 1;
			break;
		}
		holesz += factor * zeroptrs;

		factor *= sdp->sd_inptrs;
		if (hgt && (mp->mp_list[hgt - 1] < mp_eof.mp_list[hgt - 1]))
			(mp->mp_list[hgt - 1])++;
	}
	return holesz << inode->i_blkbits;
}

static void gfs2_stuffed_iomap(struct inode *inode, struct iomap *iomap)
{
	struct gfs2_inode *ip = GFS2_I(inode);

	iomap->addr = (ip->i_no_addr << inode->i_blkbits) +
		      sizeof(struct gfs2_dinode);
	iomap->offset = 0;
	iomap->length = i_size_read(inode);
	iomap->type = IOMAP_MAPPED;
	iomap->flags = IOMAP_F_DATA_INLINE;
}

/**
 * gfs2_iomap_begin - Map blocks from an inode to disk blocks
 * @inode: The inode
 * @pos: Starting position in bytes
 * @length: Length to map, in bytes
 * @flags: iomap flags
 * @iomap: The iomap structure
 *
 * Returns: errno
 */
int gfs2_iomap_begin(struct inode *inode, loff_t pos, loff_t length,
		     unsigned flags, struct iomap *iomap)
{
	struct gfs2_inode *ip = GFS2_I(inode);
	struct gfs2_sbd *sdp = GFS2_SB(inode);
	struct metapath mp = { .mp_aheight = 1, };
	unsigned int factor = sdp->sd_sb.sb_bsize;
	const u64 *arr = sdp->sd_heightsize;
	__be64 *ptr;
	sector_t lblock;
	sector_t lend;
	int ret = 0;
	int eob;
	unsigned int len;
	struct buffer_head *bh;
	u8 height;

	trace_gfs2_iomap_start(ip, pos, length, flags);
	if (!length) {
		ret = -EINVAL;
		goto out;
	}

	if (gfs2_is_stuffed(ip)) {
		if (flags & IOMAP_REPORT) {
			gfs2_stuffed_iomap(inode, iomap);
			if (pos >= iomap->length)
				ret = -ENOENT;
			goto out;
		}
		BUG_ON(!(flags & IOMAP_WRITE));
	}

	lblock = pos >> inode->i_blkbits;
	lend = (pos + length + sdp->sd_sb.sb_bsize - 1) >> inode->i_blkbits;

	iomap->offset = lblock << inode->i_blkbits;
	iomap->addr = IOMAP_NULL_ADDR;
	iomap->type = IOMAP_HOLE;
	iomap->length = (u64)(lend - lblock) << inode->i_blkbits;
	iomap->flags = IOMAP_F_MERGED;
	bmap_lock(ip, flags & IOMAP_WRITE);

	/*
	 * Directory data blocks have a struct gfs2_meta_header header, so the
	 * remaining size is smaller than the filesystem block size.  Logical
	 * block numbers for directories are in units of this remaining size!
	 */
	if (gfs2_is_dir(ip)) {
		factor = sdp->sd_jbsize;
		arr = sdp->sd_jheightsize;
	}

	ret = gfs2_meta_inode_buffer(ip, &mp.mp_bh[0]);
	if (ret)
		goto out_release;

	height = ip->i_height;
	while ((lblock + 1) * factor > arr[height])
		height++;
	find_metapath(sdp, lblock, &mp, height);
	if (height > ip->i_height || gfs2_is_stuffed(ip))
		goto do_alloc;

	ret = lookup_metapath(ip, &mp);
	if (ret)
		goto out_release;

	if (mp.mp_aheight != ip->i_height)
		goto do_alloc;

	ptr = metapointer(ip->i_height - 1, &mp);
	if (*ptr == 0)
		goto do_alloc;

	iomap->type = IOMAP_MAPPED;
	iomap->addr = be64_to_cpu(*ptr) << inode->i_blkbits;

	bh = mp.mp_bh[ip->i_height - 1];
	len = gfs2_extent_length(bh->b_data, bh->b_size, ptr, lend - lblock, &eob);
	if (eob)
		iomap->flags |= IOMAP_F_BOUNDARY;
	iomap->length = (u64)len << inode->i_blkbits;

out_release:
	release_metapath(&mp);
	bmap_unlock(ip, flags & IOMAP_WRITE);
out:
	trace_gfs2_iomap_end(ip, iomap, ret);
	return ret;

do_alloc:
	if (flags & IOMAP_WRITE) {
		ret = gfs2_iomap_alloc(inode, iomap, flags, &mp);
	} else if (flags & IOMAP_REPORT) {
		loff_t size = i_size_read(inode);
		if (pos >= size)
			ret = -ENOENT;
		else if (height <= ip->i_height)
			iomap->length = hole_size(inode, lblock, &mp);
		else
			iomap->length = size - pos;
	}
	goto out_release;
}

/**
 * gfs2_block_map - Map one or more blocks of an inode to a disk block
 * @inode: The inode
 * @lblock: The logical block number
 * @bh_map: The bh to be mapped
 * @create: True if its ok to alloc blocks to satify the request
 *
 * The size of the requested mapping is defined in bh_map->b_size.
 *
 * Clears buffer_mapped(bh_map) and leaves bh_map->b_size unchanged
 * when @lblock is not mapped.  Sets buffer_mapped(bh_map) and
 * bh_map->b_size to indicate the size of the mapping when @lblock and
 * successive blocks are mapped, up to the requested size.
 *
 * Sets buffer_boundary() if a read of metadata will be required
 * before the next block can be mapped. Sets buffer_new() if new
 * blocks were allocated.
 *
 * Returns: errno
 */

int gfs2_block_map(struct inode *inode, sector_t lblock,
		   struct buffer_head *bh_map, int create)
{
	struct gfs2_inode *ip = GFS2_I(inode);
	struct iomap iomap;
	int ret, flags = 0;

	clear_buffer_mapped(bh_map);
	clear_buffer_new(bh_map);
	clear_buffer_boundary(bh_map);
	trace_gfs2_bmap(ip, bh_map, lblock, create, 1);

	if (create)
		flags |= IOMAP_WRITE;
	ret = gfs2_iomap_begin(inode, (loff_t)lblock << inode->i_blkbits,
			       bh_map->b_size, flags, &iomap);
	if (ret) {
		if (!create && ret == -ENOENT) {
			/* Return unmapped buffer beyond the end of file.  */
			ret = 0;
		}
		goto out;
	}

	if (iomap.length > bh_map->b_size) {
		iomap.length = bh_map->b_size;
		iomap.flags &= ~IOMAP_F_BOUNDARY;
	}
	if (iomap.addr != IOMAP_NULL_ADDR)
		map_bh(bh_map, inode->i_sb, iomap.addr >> inode->i_blkbits);
	bh_map->b_size = iomap.length;
	if (iomap.flags & IOMAP_F_BOUNDARY)
		set_buffer_boundary(bh_map);
	if (iomap.flags & IOMAP_F_NEW)
		set_buffer_new(bh_map);

out:
	trace_gfs2_bmap(ip, bh_map, lblock, create, ret);
	return ret;
}

/*
 * Deprecated: do not use in new code
 */
int gfs2_extent_map(struct inode *inode, u64 lblock, int *new, u64 *dblock, unsigned *extlen)
{
	struct buffer_head bh = { .b_state = 0, .b_blocknr = 0 };
	int ret;
	int create = *new;

	BUG_ON(!extlen);
	BUG_ON(!dblock);
	BUG_ON(!new);

	bh.b_size = BIT(inode->i_blkbits + (create ? 0 : 5));
	ret = gfs2_block_map(inode, lblock, &bh, create);
	*extlen = bh.b_size >> inode->i_blkbits;
	*dblock = bh.b_blocknr;
	if (buffer_new(&bh))
		*new = 1;
	else
		*new = 0;
	return ret;
}

/**
 * gfs2_block_zero_range - Deal with zeroing out data
 *
 * This is partly borrowed from ext3.
 */
static int gfs2_block_zero_range(struct inode *inode, loff_t from,
				 unsigned int length)
{
	struct address_space *mapping = inode->i_mapping;
	struct gfs2_inode *ip = GFS2_I(inode);
	unsigned long index = from >> PAGE_SHIFT;
	unsigned offset = from & (PAGE_SIZE-1);
	unsigned blocksize, iblock, pos;
	struct buffer_head *bh;
	struct page *page;
	int err;

	page = find_or_create_page(mapping, index, GFP_NOFS);
	if (!page)
		return 0;

	blocksize = inode->i_sb->s_blocksize;
	iblock = index << (PAGE_SHIFT - inode->i_sb->s_blocksize_bits);

	if (!page_has_buffers(page))
		create_empty_buffers(page, blocksize, 0);

	/* Find the buffer that contains "offset" */
	bh = page_buffers(page);
	pos = blocksize;
	while (offset >= pos) {
		bh = bh->b_this_page;
		iblock++;
		pos += blocksize;
	}

	err = 0;

	if (!buffer_mapped(bh)) {
		gfs2_block_map(inode, iblock, bh, 0);
		/* unmapped? It's a hole - nothing to do */
		if (!buffer_mapped(bh))
			goto unlock;
	}

	/* Ok, it's mapped. Make sure it's up-to-date */
	if (PageUptodate(page))
		set_buffer_uptodate(bh);

	if (!buffer_uptodate(bh)) {
		err = -EIO;
		ll_rw_block(REQ_OP_READ, 0, 1, &bh);
		wait_on_buffer(bh);
		/* Uhhuh. Read error. Complain and punt. */
		if (!buffer_uptodate(bh))
			goto unlock;
		err = 0;
	}

	if (!gfs2_is_writeback(ip))
		gfs2_trans_add_data(ip->i_gl, bh);

	zero_user(page, offset, length);
	mark_buffer_dirty(bh);
unlock:
	unlock_page(page);
	put_page(page);
	return err;
}

#define GFS2_JTRUNC_REVOKES 8192

/**
 * gfs2_journaled_truncate - Wrapper for truncate_pagecache for jdata files
 * @inode: The inode being truncated
 * @oldsize: The original (larger) size
 * @newsize: The new smaller size
 *
 * With jdata files, we have to journal a revoke for each block which is
 * truncated. As a result, we need to split this into separate transactions
 * if the number of pages being truncated gets too large.
 */

static int gfs2_journaled_truncate(struct inode *inode, u64 oldsize, u64 newsize)
{
	struct gfs2_sbd *sdp = GFS2_SB(inode);
	u64 max_chunk = GFS2_JTRUNC_REVOKES * sdp->sd_vfs->s_blocksize;
	u64 chunk;
	int error;

	while (oldsize != newsize) {
		struct gfs2_trans *tr;
		unsigned int offs;

		chunk = oldsize - newsize;
		if (chunk > max_chunk)
			chunk = max_chunk;

		offs = oldsize & ~PAGE_MASK;
		if (offs && chunk > PAGE_SIZE)
			chunk = offs + ((chunk - offs) & PAGE_MASK);

		truncate_pagecache(inode, oldsize - chunk);
		oldsize -= chunk;

		tr = current->journal_info;
		if (!test_bit(TR_TOUCHED, &tr->tr_flags))
			continue;

		gfs2_trans_end(sdp);
		error = gfs2_trans_begin(sdp, RES_DINODE, GFS2_JTRUNC_REVOKES);
		if (error)
			return error;
	}

	return 0;
}

static int trunc_start(struct inode *inode, u64 newsize)
{
	struct gfs2_inode *ip = GFS2_I(inode);
	struct gfs2_sbd *sdp = GFS2_SB(inode);
	struct buffer_head *dibh = NULL;
	int journaled = gfs2_is_jdata(ip);
	u64 oldsize = inode->i_size;
	int error;

	if (journaled)
		error = gfs2_trans_begin(sdp, RES_DINODE + RES_JDATA, GFS2_JTRUNC_REVOKES);
	else
		error = gfs2_trans_begin(sdp, RES_DINODE, 0);
	if (error)
		return error;

	error = gfs2_meta_inode_buffer(ip, &dibh);
	if (error)
		goto out;

	gfs2_trans_add_meta(ip->i_gl, dibh);

	if (gfs2_is_stuffed(ip)) {
		gfs2_buffer_clear_tail(dibh, sizeof(struct gfs2_dinode) + newsize);
	} else {
		unsigned int blocksize = i_blocksize(inode);
		unsigned int offs = newsize & (blocksize - 1);
		if (offs) {
			error = gfs2_block_zero_range(inode, newsize,
						      blocksize - offs);
			if (error)
				goto out;
		}
		ip->i_diskflags |= GFS2_DIF_TRUNC_IN_PROG;
	}

	i_size_write(inode, newsize);
	ip->i_inode.i_mtime = ip->i_inode.i_ctime = current_time(&ip->i_inode);
	gfs2_dinode_out(ip, dibh->b_data);

	if (journaled)
		error = gfs2_journaled_truncate(inode, oldsize, newsize);
	else
		truncate_pagecache(inode, newsize);

out:
	brelse(dibh);
	if (current->journal_info)
		gfs2_trans_end(sdp);
	return error;
}

/**
 * sweep_bh_for_rgrps - find an rgrp in a meta buffer and free blocks therein
 * @ip: inode
 * @rg_gh: holder of resource group glock
 * @bh: buffer head to sweep
 * @start: starting point in bh
 * @end: end point in bh
 * @meta: true if bh points to metadata (rather than data)
 * @btotal: place to keep count of total blocks freed
 *
 * We sweep a metadata buffer (provided by the metapath) for blocks we need to
 * free, and free them all. However, we do it one rgrp at a time. If this
 * block has references to multiple rgrps, we break it into individual
 * transactions. This allows other processes to use the rgrps while we're
 * focused on a single one, for better concurrency / performance.
 * At every transaction boundary, we rewrite the inode into the journal.
 * That way the bitmaps are kept consistent with the inode and we can recover
 * if we're interrupted by power-outages.
 *
 * Returns: 0, or return code if an error occurred.
 *          *btotal has the total number of blocks freed
 */
static int sweep_bh_for_rgrps(struct gfs2_inode *ip, struct gfs2_holder *rd_gh,
			      struct buffer_head *bh, __be64 *start, __be64 *end,
			      bool meta, u32 *btotal)
{
	struct gfs2_sbd *sdp = GFS2_SB(&ip->i_inode);
	struct gfs2_rgrpd *rgd;
	struct gfs2_trans *tr;
	__be64 *p;
	int blks_outside_rgrp;
	u64 bn, bstart, isize_blks;
	s64 blen; /* needs to be s64 or gfs2_add_inode_blocks breaks */
	int ret = 0;
	bool buf_in_tr = false; /* buffer was added to transaction */

more_rgrps:
	rgd = NULL;
	if (gfs2_holder_initialized(rd_gh)) {
		rgd = gfs2_glock2rgrp(rd_gh->gh_gl);
		gfs2_assert_withdraw(sdp,
			     gfs2_glock_is_locked_by_me(rd_gh->gh_gl));
	}
	blks_outside_rgrp = 0;
	bstart = 0;
	blen = 0;

	for (p = start; p < end; p++) {
		if (!*p)
			continue;
		bn = be64_to_cpu(*p);

		if (rgd) {
			if (!rgrp_contains_block(rgd, bn)) {
				blks_outside_rgrp++;
				continue;
			}
		} else {
			rgd = gfs2_blk2rgrpd(sdp, bn, true);
			if (unlikely(!rgd)) {
				ret = -EIO;
				goto out;
			}
			ret = gfs2_glock_nq_init(rgd->rd_gl, LM_ST_EXCLUSIVE,
						 0, rd_gh);
			if (ret)
				goto out;

			/* Must be done with the rgrp glock held: */
			if (gfs2_rs_active(&ip->i_res) &&
			    rgd == ip->i_res.rs_rbm.rgd)
				gfs2_rs_deltree(&ip->i_res);
		}

		/* The size of our transactions will be unknown until we
		   actually process all the metadata blocks that relate to
		   the rgrp. So we estimate. We know it can't be more than
		   the dinode's i_blocks and we don't want to exceed the
		   journal flush threshold, sd_log_thresh2. */
		if (current->journal_info == NULL) {
			unsigned int jblocks_rqsted, revokes;

			jblocks_rqsted = rgd->rd_length + RES_DINODE +
				RES_INDIRECT;
			isize_blks = gfs2_get_inode_blocks(&ip->i_inode);
			if (isize_blks > atomic_read(&sdp->sd_log_thresh2))
				jblocks_rqsted +=
					atomic_read(&sdp->sd_log_thresh2);
			else
				jblocks_rqsted += isize_blks;
			revokes = jblocks_rqsted;
			if (meta)
				revokes += end - start;
			else if (ip->i_depth)
				revokes += sdp->sd_inptrs;
			ret = gfs2_trans_begin(sdp, jblocks_rqsted, revokes);
			if (ret)
				goto out_unlock;
			down_write(&ip->i_rw_mutex);
		}
		/* check if we will exceed the transaction blocks requested */
		tr = current->journal_info;
		if (tr->tr_num_buf_new + RES_STATFS +
		    RES_QUOTA >= atomic_read(&sdp->sd_log_thresh2)) {
			/* We set blks_outside_rgrp to ensure the loop will
			   be repeated for the same rgrp, but with a new
			   transaction. */
			blks_outside_rgrp++;
			/* This next part is tricky. If the buffer was added
			   to the transaction, we've already set some block
			   pointers to 0, so we better follow through and free
			   them, or we will introduce corruption (so break).
			   This may be impossible, or at least rare, but I
			   decided to cover the case regardless.

			   If the buffer was not added to the transaction
			   (this call), doing so would exceed our transaction
			   size, so we need to end the transaction and start a
			   new one (so goto). */

			if (buf_in_tr)
				break;
			goto out_unlock;
		}

		gfs2_trans_add_meta(ip->i_gl, bh);
		buf_in_tr = true;
		*p = 0;
		if (bstart + blen == bn) {
			blen++;
			continue;
		}
		if (bstart) {
			__gfs2_free_blocks(ip, bstart, (u32)blen, meta);
			(*btotal) += blen;
			gfs2_add_inode_blocks(&ip->i_inode, -blen);
		}
		bstart = bn;
		blen = 1;
	}
	if (bstart) {
		__gfs2_free_blocks(ip, bstart, (u32)blen, meta);
		(*btotal) += blen;
		gfs2_add_inode_blocks(&ip->i_inode, -blen);
	}
out_unlock:
	if (!ret && blks_outside_rgrp) { /* If buffer still has non-zero blocks
					    outside the rgrp we just processed,
					    do it all over again. */
		if (current->journal_info) {
			struct buffer_head *dibh;

			ret = gfs2_meta_inode_buffer(ip, &dibh);
			if (ret)
				goto out;

			/* Every transaction boundary, we rewrite the dinode
			   to keep its di_blocks current in case of failure. */
			ip->i_inode.i_mtime = ip->i_inode.i_ctime =
				current_time(&ip->i_inode);
			gfs2_trans_add_meta(ip->i_gl, dibh);
			gfs2_dinode_out(ip, dibh->b_data);
			brelse(dibh);
			up_write(&ip->i_rw_mutex);
			gfs2_trans_end(sdp);
		}
		gfs2_glock_dq_uninit(rd_gh);
		cond_resched();
		goto more_rgrps;
	}
out:
	return ret;
}

static bool mp_eq_to_hgt(struct metapath *mp, __u16 *list, unsigned int h)
{
	if (memcmp(mp->mp_list, list, h * sizeof(mp->mp_list[0])))
		return false;
	return true;
}

/**
 * find_nonnull_ptr - find a non-null pointer given a metapath and height
 * @mp: starting metapath
 * @h: desired height to search
 *
 * Assumes the metapath is valid (with buffers) out to height h.
 * Returns: true if a non-null pointer was found in the metapath buffer
 *          false if all remaining pointers are NULL in the buffer
 */
static bool find_nonnull_ptr(struct gfs2_sbd *sdp, struct metapath *mp,
			     unsigned int h,
			     __u16 *end_list, unsigned int end_aligned)
{
	struct buffer_head *bh = mp->mp_bh[h];
	__be64 *first, *ptr, *end;

	first = metaptr1(h, mp);
	ptr = first + mp->mp_list[h];
	end = (__be64 *)(bh->b_data + bh->b_size);
	if (end_list && mp_eq_to_hgt(mp, end_list, h)) {
		bool keep_end = h < end_aligned;
		end = first + end_list[h] + keep_end;
	}

	while (ptr < end) {
		if (*ptr) { /* if we have a non-null pointer */
			mp->mp_list[h] = ptr - first;
			h++;
			if (h < GFS2_MAX_META_HEIGHT)
				mp->mp_list[h] = 0;
			return true;
		}
		ptr++;
	}
	return false;
}

enum dealloc_states {
	DEALLOC_MP_FULL = 0,    /* Strip a metapath with all buffers read in */
	DEALLOC_MP_LOWER = 1,   /* lower the metapath strip height */
	DEALLOC_FILL_MP = 2,  /* Fill in the metapath to the given height. */
	DEALLOC_DONE = 3,       /* process complete */
};

static inline void
metapointer_range(struct metapath *mp, int height,
		  __u16 *start_list, unsigned int start_aligned,
		  __u16 *end_list, unsigned int end_aligned,
		  __be64 **start, __be64 **end)
{
	struct buffer_head *bh = mp->mp_bh[height];
	__be64 *first;

	first = metaptr1(height, mp);
	*start = first;
	if (mp_eq_to_hgt(mp, start_list, height)) {
		bool keep_start = height < start_aligned;
		*start = first + start_list[height] + keep_start;
	}
	*end = (__be64 *)(bh->b_data + bh->b_size);
	if (end_list && mp_eq_to_hgt(mp, end_list, height)) {
		bool keep_end = height < end_aligned;
		*end = first + end_list[height] + keep_end;
	}
}

static inline bool walk_done(struct gfs2_sbd *sdp,
			     struct metapath *mp, int height,
			     __u16 *end_list, unsigned int end_aligned)
{
	__u16 end;

	if (end_list) {
		bool keep_end = height < end_aligned;
		if (!mp_eq_to_hgt(mp, end_list, height))
			return false;
		end = end_list[height] + keep_end;
	} else
		end = (height > 0) ? sdp->sd_inptrs : sdp->sd_diptrs;
	return mp->mp_list[height] >= end;
}

/**
 * punch_hole - deallocate blocks in a file
 * @ip: inode to truncate
 * @offset: the start of the hole
 * @length: the size of the hole (or 0 for truncate)
 *
 * Punch a hole into a file or truncate a file at a given position.  This
 * function operates in whole blocks (@offset and @length are rounded
 * accordingly); partially filled blocks must be cleared otherwise.
 *
 * This function works from the bottom up, and from the right to the left. In
 * other words, it strips off the highest layer (data) before stripping any of
 * the metadata. Doing it this way is best in case the operation is interrupted
 * by power failure, etc.  The dinode is rewritten in every transaction to
 * guarantee integrity.
 */
static int punch_hole(struct gfs2_inode *ip, u64 offset, u64 length)
{
	struct gfs2_sbd *sdp = GFS2_SB(&ip->i_inode);
	u64 maxsize = sdp->sd_heightsize[ip->i_height];
	struct metapath mp = {};
	struct buffer_head *dibh, *bh;
	struct gfs2_holder rd_gh;
	unsigned int bsize_shift = sdp->sd_sb.sb_bsize_shift;
	u64 lblock = (offset + (1 << bsize_shift) - 1) >> bsize_shift;
	__u16 start_list[GFS2_MAX_META_HEIGHT];
	__u16 __end_list[GFS2_MAX_META_HEIGHT], *end_list = NULL;
	unsigned int start_aligned, uninitialized_var(end_aligned);
	unsigned int strip_h = ip->i_height - 1;
	u32 btotal = 0;
	int ret, state;
	int mp_h; /* metapath buffers are read in to this height */
	u64 prev_bnr = 0;
	__be64 *start, *end;

	if (offset >= maxsize) {
		/*
		 * The starting point lies beyond the allocated meta-data;
		 * there are no blocks do deallocate.
		 */
		return 0;
	}

	/*
	 * The start position of the hole is defined by lblock, start_list, and
	 * start_aligned.  The end position of the hole is defined by lend,
	 * end_list, and end_aligned.
	 *
	 * start_aligned and end_aligned define down to which height the start
	 * and end positions are aligned to the metadata tree (i.e., the
	 * position is a multiple of the metadata granularity at the height
	 * above).  This determines at which heights additional meta pointers
	 * needs to be preserved for the remaining data.
	 */

	if (length) {
		u64 end_offset = offset + length;
		u64 lend;

		/*
		 * Clip the end at the maximum file size for the given height:
		 * that's how far the metadata goes; files bigger than that
		 * will have additional layers of indirection.
		 */
		if (end_offset > maxsize)
			end_offset = maxsize;
		lend = end_offset >> bsize_shift;

		if (lblock >= lend)
			return 0;

		find_metapath(sdp, lend, &mp, ip->i_height);
		end_list = __end_list;
		memcpy(end_list, mp.mp_list, sizeof(mp.mp_list));

		for (mp_h = ip->i_height - 1; mp_h > 0; mp_h--) {
			if (end_list[mp_h])
				break;
		}
		end_aligned = mp_h;
	}

	find_metapath(sdp, lblock, &mp, ip->i_height);
	memcpy(start_list, mp.mp_list, sizeof(start_list));

	for (mp_h = ip->i_height - 1; mp_h > 0; mp_h--) {
		if (start_list[mp_h])
			break;
	}
	start_aligned = mp_h;

	ret = gfs2_meta_inode_buffer(ip, &dibh);
	if (ret)
		return ret;

	mp.mp_bh[0] = dibh;
	ret = lookup_metapath(ip, &mp);
	if (ret)
		goto out_metapath;

	/* issue read-ahead on metadata */
	for (mp_h = 0; mp_h < mp.mp_aheight - 1; mp_h++) {
		metapointer_range(&mp, mp_h, start_list, start_aligned,
				  end_list, end_aligned, &start, &end);
		gfs2_metapath_ra(ip->i_gl, start, end);
	}

	if (mp.mp_aheight == ip->i_height)
		state = DEALLOC_MP_FULL; /* We have a complete metapath */
	else
		state = DEALLOC_FILL_MP; /* deal with partial metapath */

	ret = gfs2_rindex_update(sdp);
	if (ret)
		goto out_metapath;

	ret = gfs2_quota_hold(ip, NO_UID_QUOTA_CHANGE, NO_GID_QUOTA_CHANGE);
	if (ret)
		goto out_metapath;
	gfs2_holder_mark_uninitialized(&rd_gh);

	mp_h = strip_h;

	while (state != DEALLOC_DONE) {
		switch (state) {
		/* Truncate a full metapath at the given strip height.
		 * Note that strip_h == mp_h in order to be in this state. */
		case DEALLOC_MP_FULL:
			bh = mp.mp_bh[mp_h];
			gfs2_assert_withdraw(sdp, bh);
			if (gfs2_assert_withdraw(sdp,
						 prev_bnr != bh->b_blocknr)) {
				printk(KERN_EMERG "GFS2: fsid=%s:inode %llu, "
				       "block:%llu, i_h:%u, s_h:%u, mp_h:%u\n",
				       sdp->sd_fsname,
				       (unsigned long long)ip->i_no_addr,
				       prev_bnr, ip->i_height, strip_h, mp_h);
			}
			prev_bnr = bh->b_blocknr;

			if (gfs2_metatype_check(sdp, bh,
						(mp_h ? GFS2_METATYPE_IN :
							GFS2_METATYPE_DI))) {
				ret = -EIO;
				goto out;
			}

			/*
			 * Below, passing end_aligned as 0 gives us the
			 * metapointer range excluding the end point: the end
			 * point is the first metapath we must not deallocate!
			 */

			metapointer_range(&mp, mp_h, start_list, start_aligned,
					  end_list, 0 /* end_aligned */,
					  &start, &end);
			ret = sweep_bh_for_rgrps(ip, &rd_gh, mp.mp_bh[mp_h],
						 start, end,
						 mp_h != ip->i_height - 1,
						 &btotal);

			/* If we hit an error or just swept dinode buffer,
			   just exit. */
			if (ret || !mp_h) {
				state = DEALLOC_DONE;
				break;
			}
			state = DEALLOC_MP_LOWER;
			break;

		/* lower the metapath strip height */
		case DEALLOC_MP_LOWER:
			/* We're done with the current buffer, so release it,
			   unless it's the dinode buffer. Then back up to the
			   previous pointer. */
			if (mp_h) {
				brelse(mp.mp_bh[mp_h]);
				mp.mp_bh[mp_h] = NULL;
			}
			/* If we can't get any lower in height, we've stripped
			   off all we can. Next step is to back up and start
			   stripping the previous level of metadata. */
			if (mp_h == 0) {
				strip_h--;
				memcpy(mp.mp_list, start_list, sizeof(start_list));
				mp_h = strip_h;
				state = DEALLOC_FILL_MP;
				break;
			}
			mp.mp_list[mp_h] = 0;
			mp_h--; /* search one metadata height down */
			mp.mp_list[mp_h]++;
			if (walk_done(sdp, &mp, mp_h, end_list, end_aligned))
				break;
			/* Here we've found a part of the metapath that is not
			 * allocated. We need to search at that height for the
			 * next non-null pointer. */
			if (find_nonnull_ptr(sdp, &mp, mp_h, end_list, end_aligned)) {
				state = DEALLOC_FILL_MP;
				mp_h++;
			}
			/* No more non-null pointers at this height. Back up
			   to the previous height and try again. */
			break; /* loop around in the same state */

		/* Fill the metapath with buffers to the given height. */
		case DEALLOC_FILL_MP:
			/* Fill the buffers out to the current height. */
			ret = fillup_metapath(ip, &mp, mp_h);
			if (ret < 0)
				goto out;

			/* issue read-ahead on metadata */
			if (mp.mp_aheight > 1) {
				for (; ret > 1; ret--) {
					metapointer_range(&mp, mp.mp_aheight - ret,
							  start_list, start_aligned,
							  end_list, end_aligned,
							  &start, &end);
					gfs2_metapath_ra(ip->i_gl, start, end);
				}
			}

			/* If buffers found for the entire strip height */
			if (mp.mp_aheight - 1 == strip_h) {
				state = DEALLOC_MP_FULL;
				break;
			}
			if (mp.mp_aheight < ip->i_height) /* We have a partial height */
				mp_h = mp.mp_aheight - 1;

			/* If we find a non-null block pointer, crawl a bit
			   higher up in the metapath and try again, otherwise
			   we need to look lower for a new starting point. */
			if (find_nonnull_ptr(sdp, &mp, mp_h, end_list, end_aligned))
				mp_h++;
			else
				state = DEALLOC_MP_LOWER;
			break;
		}
	}

	if (btotal) {
		if (current->journal_info == NULL) {
			ret = gfs2_trans_begin(sdp, RES_DINODE + RES_STATFS +
					       RES_QUOTA, 0);
			if (ret)
				goto out;
			down_write(&ip->i_rw_mutex);
		}
		gfs2_statfs_change(sdp, 0, +btotal, 0);
		gfs2_quota_change(ip, -(s64)btotal, ip->i_inode.i_uid,
				  ip->i_inode.i_gid);
		ip->i_inode.i_mtime = ip->i_inode.i_ctime = current_time(&ip->i_inode);
		gfs2_trans_add_meta(ip->i_gl, dibh);
		gfs2_dinode_out(ip, dibh->b_data);
		up_write(&ip->i_rw_mutex);
		gfs2_trans_end(sdp);
	}

out:
	if (gfs2_holder_initialized(&rd_gh))
		gfs2_glock_dq_uninit(&rd_gh);
	if (current->journal_info) {
		up_write(&ip->i_rw_mutex);
		gfs2_trans_end(sdp);
		cond_resched();
	}
	gfs2_quota_unhold(ip);
out_metapath:
	release_metapath(&mp);
	return ret;
}

static int trunc_end(struct gfs2_inode *ip)
{
	struct gfs2_sbd *sdp = GFS2_SB(&ip->i_inode);
	struct buffer_head *dibh;
	int error;

	error = gfs2_trans_begin(sdp, RES_DINODE, 0);
	if (error)
		return error;

	down_write(&ip->i_rw_mutex);

	error = gfs2_meta_inode_buffer(ip, &dibh);
	if (error)
		goto out;

	if (!i_size_read(&ip->i_inode)) {
		ip->i_height = 0;
		ip->i_goal = ip->i_no_addr;
		gfs2_buffer_clear_tail(dibh, sizeof(struct gfs2_dinode));
		gfs2_ordered_del_inode(ip);
	}
	ip->i_inode.i_mtime = ip->i_inode.i_ctime = current_time(&ip->i_inode);
	ip->i_diskflags &= ~GFS2_DIF_TRUNC_IN_PROG;

	gfs2_trans_add_meta(ip->i_gl, dibh);
	gfs2_dinode_out(ip, dibh->b_data);
	brelse(dibh);

out:
	up_write(&ip->i_rw_mutex);
	gfs2_trans_end(sdp);
	return error;
}

/**
 * do_shrink - make a file smaller
 * @inode: the inode
 * @newsize: the size to make the file
 *
 * Called with an exclusive lock on @inode. The @size must
 * be equal to or smaller than the current inode size.
 *
 * Returns: errno
 */

static int do_shrink(struct inode *inode, u64 newsize)
{
	struct gfs2_inode *ip = GFS2_I(inode);
	int error;

	error = trunc_start(inode, newsize);
	if (error < 0)
		return error;
	if (gfs2_is_stuffed(ip))
		return 0;

	error = punch_hole(ip, newsize, 0);
	if (error == 0)
		error = trunc_end(ip);

	return error;
}

void gfs2_trim_blocks(struct inode *inode)
{
	int ret;

	ret = do_shrink(inode, inode->i_size);
	WARN_ON(ret != 0);
}

/**
 * do_grow - Touch and update inode size
 * @inode: The inode
 * @size: The new size
 *
 * This function updates the timestamps on the inode and
 * may also increase the size of the inode. This function
 * must not be called with @size any smaller than the current
 * inode size.
 *
 * Although it is not strictly required to unstuff files here,
 * earlier versions of GFS2 have a bug in the stuffed file reading
 * code which will result in a buffer overrun if the size is larger
 * than the max stuffed file size. In order to prevent this from
 * occurring, such files are unstuffed, but in other cases we can
 * just update the inode size directly.
 *
 * Returns: 0 on success, or -ve on error
 */

static int do_grow(struct inode *inode, u64 size)
{
	struct gfs2_inode *ip = GFS2_I(inode);
	struct gfs2_sbd *sdp = GFS2_SB(inode);
	struct gfs2_alloc_parms ap = { .target = 1, };
	struct buffer_head *dibh;
	int error;
	int unstuff = 0;

	if (gfs2_is_stuffed(ip) && size > gfs2_max_stuffed_size(ip)) {
		error = gfs2_quota_lock_check(ip, &ap);
		if (error)
			return error;

		error = gfs2_inplace_reserve(ip, &ap);
		if (error)
			goto do_grow_qunlock;
		unstuff = 1;
	}

	error = gfs2_trans_begin(sdp, RES_DINODE + RES_STATFS + RES_RG_BIT +
				 (sdp->sd_args.ar_quota == GFS2_QUOTA_OFF ?
				  0 : RES_QUOTA), 0);
	if (error)
		goto do_grow_release;

	if (unstuff) {
		error = gfs2_unstuff_dinode(ip, NULL);
		if (error)
			goto do_end_trans;
	}

	error = gfs2_meta_inode_buffer(ip, &dibh);
	if (error)
		goto do_end_trans;

	i_size_write(inode, size);
	ip->i_inode.i_mtime = ip->i_inode.i_ctime = current_time(&ip->i_inode);
	gfs2_trans_add_meta(ip->i_gl, dibh);
	gfs2_dinode_out(ip, dibh->b_data);
	brelse(dibh);

do_end_trans:
	gfs2_trans_end(sdp);
do_grow_release:
	if (unstuff) {
		gfs2_inplace_release(ip);
do_grow_qunlock:
		gfs2_quota_unlock(ip);
	}
	return error;
}

/**
 * gfs2_setattr_size - make a file a given size
 * @inode: the inode
 * @newsize: the size to make the file
 *
 * The file size can grow, shrink, or stay the same size. This
 * is called holding i_rwsem and an exclusive glock on the inode
 * in question.
 *
 * Returns: errno
 */

int gfs2_setattr_size(struct inode *inode, u64 newsize)
{
	struct gfs2_inode *ip = GFS2_I(inode);
	int ret;

	BUG_ON(!S_ISREG(inode->i_mode));

	ret = inode_newsize_ok(inode, newsize);
	if (ret)
		return ret;

	inode_dio_wait(inode);

	ret = gfs2_rsqa_alloc(ip);
	if (ret)
		goto out;

	if (newsize >= inode->i_size) {
		ret = do_grow(inode, newsize);
		goto out;
	}

	ret = do_shrink(inode, newsize);
out:
	gfs2_rsqa_delete(ip, NULL);
	return ret;
}

int gfs2_truncatei_resume(struct gfs2_inode *ip)
{
	int error;
	error = punch_hole(ip, i_size_read(&ip->i_inode), 0);
	if (!error)
		error = trunc_end(ip);
	return error;
}

int gfs2_file_dealloc(struct gfs2_inode *ip)
{
	return punch_hole(ip, 0, 0);
}

/**
 * gfs2_free_journal_extents - Free cached journal bmap info
 * @jd: The journal
 *
 */

void gfs2_free_journal_extents(struct gfs2_jdesc *jd)
{
	struct gfs2_journal_extent *jext;

	while(!list_empty(&jd->extent_list)) {
		jext = list_entry(jd->extent_list.next, struct gfs2_journal_extent, list);
		list_del(&jext->list);
		kfree(jext);
	}
}

/**
 * gfs2_add_jextent - Add or merge a new extent to extent cache
 * @jd: The journal descriptor
 * @lblock: The logical block at start of new extent
 * @dblock: The physical block at start of new extent
 * @blocks: Size of extent in fs blocks
 *
 * Returns: 0 on success or -ENOMEM
 */

static int gfs2_add_jextent(struct gfs2_jdesc *jd, u64 lblock, u64 dblock, u64 blocks)
{
	struct gfs2_journal_extent *jext;

	if (!list_empty(&jd->extent_list)) {
		jext = list_entry(jd->extent_list.prev, struct gfs2_journal_extent, list);
		if ((jext->dblock + jext->blocks) == dblock) {
			jext->blocks += blocks;
			return 0;
		}
	}

	jext = kzalloc(sizeof(struct gfs2_journal_extent), GFP_NOFS);
	if (jext == NULL)
		return -ENOMEM;
	jext->dblock = dblock;
	jext->lblock = lblock;
	jext->blocks = blocks;
	list_add_tail(&jext->list, &jd->extent_list);
	jd->nr_extents++;
	return 0;
}

/**
 * gfs2_map_journal_extents - Cache journal bmap info
 * @sdp: The super block
 * @jd: The journal to map
 *
 * Create a reusable "extent" mapping from all logical
 * blocks to all physical blocks for the given journal.  This will save
 * us time when writing journal blocks.  Most journals will have only one
 * extent that maps all their logical blocks.  That's because gfs2.mkfs
 * arranges the journal blocks sequentially to maximize performance.
 * So the extent would map the first block for the entire file length.
 * However, gfs2_jadd can happen while file activity is happening, so
 * those journals may not be sequential.  Less likely is the case where
 * the users created their own journals by mounting the metafs and
 * laying it out.  But it's still possible.  These journals might have
 * several extents.
 *
 * Returns: 0 on success, or error on failure
 */

int gfs2_map_journal_extents(struct gfs2_sbd *sdp, struct gfs2_jdesc *jd)
{
	u64 lblock = 0;
	u64 lblock_stop;
	struct gfs2_inode *ip = GFS2_I(jd->jd_inode);
	struct buffer_head bh;
	unsigned int shift = sdp->sd_sb.sb_bsize_shift;
	u64 size;
	int rc;

	lblock_stop = i_size_read(jd->jd_inode) >> shift;
	size = (lblock_stop - lblock) << shift;
	jd->nr_extents = 0;
	WARN_ON(!list_empty(&jd->extent_list));

	do {
		bh.b_state = 0;
		bh.b_blocknr = 0;
		bh.b_size = size;
		rc = gfs2_block_map(jd->jd_inode, lblock, &bh, 0);
		if (rc || !buffer_mapped(&bh))
			goto fail;
		rc = gfs2_add_jextent(jd, lblock, bh.b_blocknr, bh.b_size >> shift);
		if (rc)
			goto fail;
		size -= bh.b_size;
		lblock += (bh.b_size >> ip->i_inode.i_blkbits);
	} while(size > 0);

	fs_info(sdp, "journal %d mapped with %u extents\n", jd->jd_jid,
		jd->nr_extents);
	return 0;

fail:
	fs_warn(sdp, "error %d mapping journal %u at offset %llu (extent %u)\n",
		rc, jd->jd_jid,
		(unsigned long long)(i_size_read(jd->jd_inode) - size),
		jd->nr_extents);
	fs_warn(sdp, "bmap=%d lblock=%llu block=%llu, state=0x%08lx, size=%llu\n",
		rc, (unsigned long long)lblock, (unsigned long long)bh.b_blocknr,
		bh.b_state, (unsigned long long)bh.b_size);
	gfs2_free_journal_extents(jd);
	return rc;
}

/**
 * gfs2_write_alloc_required - figure out if a write will require an allocation
 * @ip: the file being written to
 * @offset: the offset to write to
 * @len: the number of bytes being written
 *
 * Returns: 1 if an alloc is required, 0 otherwise
 */

int gfs2_write_alloc_required(struct gfs2_inode *ip, u64 offset,
			      unsigned int len)
{
	struct gfs2_sbd *sdp = GFS2_SB(&ip->i_inode);
	struct buffer_head bh;
	unsigned int shift;
	u64 lblock, lblock_stop, size;
	u64 end_of_file;

	if (!len)
		return 0;

	if (gfs2_is_stuffed(ip)) {
		if (offset + len > gfs2_max_stuffed_size(ip))
			return 1;
		return 0;
	}

	shift = sdp->sd_sb.sb_bsize_shift;
	BUG_ON(gfs2_is_dir(ip));
	end_of_file = (i_size_read(&ip->i_inode) + sdp->sd_sb.sb_bsize - 1) >> shift;
	lblock = offset >> shift;
	lblock_stop = (offset + len + sdp->sd_sb.sb_bsize - 1) >> shift;
	if (lblock_stop > end_of_file)
		return 1;

	size = (lblock_stop - lblock) << shift;
	do {
		bh.b_state = 0;
		bh.b_size = size;
		gfs2_block_map(&ip->i_inode, lblock, &bh, 0);
		if (!buffer_mapped(&bh))
			return 1;
		size -= bh.b_size;
		lblock += (bh.b_size >> ip->i_inode.i_blkbits);
	} while(size > 0);

	return 0;
}

static int stuffed_zero_range(struct inode *inode, loff_t offset, loff_t length)
{
	struct gfs2_inode *ip = GFS2_I(inode);
	struct buffer_head *dibh;
	int error;

	if (offset >= inode->i_size)
		return 0;
	if (offset + length > inode->i_size)
		length = inode->i_size - offset;

	error = gfs2_meta_inode_buffer(ip, &dibh);
	if (error)
		return error;
	gfs2_trans_add_meta(ip->i_gl, dibh);
	memset(dibh->b_data + sizeof(struct gfs2_dinode) + offset, 0,
	       length);
	brelse(dibh);
	return 0;
}

static int gfs2_journaled_truncate_range(struct inode *inode, loff_t offset,
					 loff_t length)
{
	struct gfs2_sbd *sdp = GFS2_SB(inode);
	loff_t max_chunk = GFS2_JTRUNC_REVOKES * sdp->sd_vfs->s_blocksize;
	int error;

	while (length) {
		struct gfs2_trans *tr;
		loff_t chunk;
		unsigned int offs;

		chunk = length;
		if (chunk > max_chunk)
			chunk = max_chunk;

		offs = offset & ~PAGE_MASK;
		if (offs && chunk > PAGE_SIZE)
			chunk = offs + ((chunk - offs) & PAGE_MASK);

		truncate_pagecache_range(inode, offset, chunk);
		offset += chunk;
		length -= chunk;

		tr = current->journal_info;
		if (!test_bit(TR_TOUCHED, &tr->tr_flags))
			continue;

		gfs2_trans_end(sdp);
		error = gfs2_trans_begin(sdp, RES_DINODE, GFS2_JTRUNC_REVOKES);
		if (error)
			return error;
	}
	return 0;
}

int __gfs2_punch_hole(struct file *file, loff_t offset, loff_t length)
{
	struct inode *inode = file_inode(file);
	struct gfs2_inode *ip = GFS2_I(inode);
	struct gfs2_sbd *sdp = GFS2_SB(inode);
	int error;

	if (gfs2_is_jdata(ip))
		error = gfs2_trans_begin(sdp, RES_DINODE + 2 * RES_JDATA,
					 GFS2_JTRUNC_REVOKES);
	else
		error = gfs2_trans_begin(sdp, RES_DINODE, 0);
	if (error)
		return error;

	if (gfs2_is_stuffed(ip)) {
		error = stuffed_zero_range(inode, offset, length);
		if (error)
			goto out;
	} else {
		unsigned int start_off, end_off, blocksize;

		blocksize = i_blocksize(inode);
		start_off = offset & (blocksize - 1);
		end_off = (offset + length) & (blocksize - 1);
		if (start_off) {
			unsigned int len = length;
			if (length > blocksize - start_off)
				len = blocksize - start_off;
			error = gfs2_block_zero_range(inode, offset, len);
			if (error)
				goto out;
			if (start_off + length < blocksize)
				end_off = 0;
		}
		if (end_off) {
			error = gfs2_block_zero_range(inode,
				offset + length - end_off, end_off);
			if (error)
				goto out;
		}
	}

	if (gfs2_is_jdata(ip)) {
		BUG_ON(!current->journal_info);
		gfs2_journaled_truncate_range(inode, offset, length);
	} else
		truncate_pagecache_range(inode, offset, offset + length - 1);

	file_update_time(file);
	mark_inode_dirty(inode);

	if (current->journal_info)
		gfs2_trans_end(sdp);

	if (!gfs2_is_stuffed(ip))
		error = punch_hole(ip, offset, length);

out:
	if (current->journal_info)
		gfs2_trans_end(sdp);
	return error;
}
