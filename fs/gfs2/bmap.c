// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) Sistina Software, Inc.  1997-2003 All rights reserved.
 * Copyright (C) 2004-2006 Red Hat, Inc.  All rights reserved.
 */

#include <linux/spinlock.h>
#include <linux/completion.h>
#include <linux/buffer_head.h>
#include <linux/blkdev.h>
#include <linux/gfs2_ondisk.h>
#include <linux/crc32.h>
#include <linux/iomap.h>
#include <linux/ktime.h>

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
#include "aops.h"
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

static int punch_hole(struct gfs2_inode *ip, u64 offset, u64 length);

/**
 * gfs2_unstuffer_folio - unstuff a stuffed inode into a block cached by a folio
 * @ip: the inode
 * @dibh: the dinode buffer
 * @block: the block number that was allocated
 * @folio: The folio.
 *
 * Returns: errno
 */
static int gfs2_unstuffer_folio(struct gfs2_inode *ip, struct buffer_head *dibh,
			       u64 block, struct folio *folio)
{
	struct inode *inode = &ip->i_inode;

	if (!folio_test_uptodate(folio)) {
		void *kaddr = kmap_local_folio(folio, 0);
		u64 dsize = i_size_read(inode);
 
		memcpy(kaddr, dibh->b_data + sizeof(struct gfs2_dinode), dsize);
		memset(kaddr + dsize, 0, folio_size(folio) - dsize);
		kunmap_local(kaddr);

		folio_mark_uptodate(folio);
	}

	if (gfs2_is_jdata(ip)) {
		struct buffer_head *bh = folio_buffers(folio);

		if (!bh)
			bh = create_empty_buffers(folio,
				BIT(inode->i_blkbits), BIT(BH_Uptodate));

		if (!buffer_mapped(bh))
			map_bh(bh, inode->i_sb, block);

		set_buffer_uptodate(bh);
		gfs2_trans_add_data(ip->i_gl, bh);
	} else {
		folio_mark_dirty(folio);
		gfs2_ordered_add_inode(ip);
	}

	return 0;
}

static int __gfs2_unstuff_inode(struct gfs2_inode *ip, struct folio *folio)
{
	struct buffer_head *bh, *dibh;
	struct gfs2_dinode *di;
	u64 block = 0;
	int isdir = gfs2_is_dir(ip);
	int error;

	error = gfs2_meta_inode_buffer(ip, &dibh);
	if (error)
		return error;

	if (i_size_read(&ip->i_inode)) {
		/* Get a free block, fill it with the stuffed data,
		   and write it out to disk */

		unsigned int n = 1;
		error = gfs2_alloc_blocks(ip, &block, &n, 0);
		if (error)
			goto out_brelse;
		if (isdir) {
			gfs2_trans_remove_revoke(GFS2_SB(&ip->i_inode), block, 1);
			error = gfs2_dir_get_new_buffer(ip, block, &bh);
			if (error)
				goto out_brelse;
			gfs2_buffer_copy_tail(bh, sizeof(struct gfs2_meta_header),
					      dibh, sizeof(struct gfs2_dinode));
			brelse(bh);
		} else {
			error = gfs2_unstuffer_folio(ip, dibh, block, folio);
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
	return error;
}

/**
 * gfs2_unstuff_dinode - Unstuff a dinode when the data has grown too big
 * @ip: The GFS2 inode to unstuff
 *
 * This routine unstuffs a dinode and returns it to a "normal" state such
 * that the height can be grown in the traditional way.
 *
 * Returns: errno
 */

int gfs2_unstuff_dinode(struct gfs2_inode *ip)
{
	struct inode *inode = &ip->i_inode;
	struct folio *folio;
	int error;

	down_write(&ip->i_rw_mutex);
	folio = filemap_grab_folio(inode->i_mapping, 0);
	error = PTR_ERR(folio);
	if (IS_ERR(folio))
		goto out;
	error = __gfs2_unstuff_inode(ip, folio);
	folio_unlock(folio);
	folio_put(folio);
out:
	up_write(&ip->i_rw_mutex);
	return error;
}

/**
 * find_metapath - Find path through the metadata tree
 * @sdp: The superblock
 * @block: The disk block to look up
 * @mp: The metapath to return the result in
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
 *   mp_fheight = 3, mp_list[0] = 0, mp_list[1] = 48, and mp_list[2] = 165.
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

static inline const __be64 *metaend(unsigned int height, const struct metapath *mp)
{
	const struct buffer_head *bh = mp->mp_bh[height];
	return (const __be64 *)(bh->b_data + bh->b_size);
}

static void clone_metapath(struct metapath *clone, struct metapath *mp)
{
	unsigned int hgt;

	*clone = *mp;
	for (hgt = 0; hgt < mp->mp_aheight; hgt++)
		get_bh(clone->mp_bh[hgt]);
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
				submit_bh(REQ_OP_READ | REQ_RAHEAD | REQ_META |
					  REQ_PRIO, rabh);
				continue;
			}
			unlock_buffer(rabh);
		}
		brelse(rabh);
	}
}

static inline struct buffer_head *
metapath_dibh(struct metapath *mp)
{
	return mp->mp_bh[0];
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
		ret = gfs2_meta_buffer(ip, GFS2_METATYPE_IN, dblock, &mp->mp_bh[x + 1]);
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

static sector_t metapath_to_block(struct gfs2_sbd *sdp, struct metapath *mp)
{
	sector_t factor = 1, block = 0;
	int hgt;

	for (hgt = mp->mp_fheight - 1; hgt >= 0; hgt--) {
		if (hgt < mp->mp_aheight)
			block += mp->mp_list[hgt] * factor;
		factor *= sdp->sd_inptrs;
	}
	return block;
}

static void release_metapath(struct metapath *mp)
{
	int i;

	for (i = 0; i < GFS2_MAX_META_HEIGHT; i++) {
		if (mp->mp_bh[i] == NULL)
			break;
		brelse(mp->mp_bh[i]);
		mp->mp_bh[i] = NULL;
	}
}

/**
 * gfs2_extent_length - Returns length of an extent of blocks
 * @bh: The metadata block
 * @ptr: Current position in @bh
 * @eob: Set to 1 if we hit "end of block"
 *
 * Returns: The length of the extent (minimum of one block)
 */

static inline unsigned int gfs2_extent_length(struct buffer_head *bh, __be64 *ptr, int *eob)
{
	const __be64 *end = (__be64 *)(bh->b_data + bh->b_size);
	const __be64 *first = ptr;
	u64 d = be64_to_cpu(*ptr);

	*eob = 0;
	do {
		ptr++;
		if (ptr >= end)
			break;
		d++;
	} while(be64_to_cpu(*ptr) == d);
	if (ptr >= end)
		*eob = 1;
	return ptr - first;
}

enum walker_status { WALK_STOP, WALK_FOLLOW, WALK_CONTINUE };

/*
 * gfs2_metadata_walker - walk an indirect block
 * @mp: Metapath to indirect block
 * @ptrs: Number of pointers to look at
 *
 * When returning WALK_FOLLOW, the walker must update @mp to point at the right
 * indirect block to follow.
 */
typedef enum walker_status (*gfs2_metadata_walker)(struct metapath *mp,
						   unsigned int ptrs);

/*
 * gfs2_walk_metadata - walk a tree of indirect blocks
 * @inode: The inode
 * @mp: Starting point of walk
 * @max_len: Maximum number of blocks to walk
 * @walker: Called during the walk
 *
 * Returns 1 if the walk was stopped by @walker, 0 if we went past @max_len or
 * past the end of metadata, and a negative error code otherwise.
 */

static int gfs2_walk_metadata(struct inode *inode, struct metapath *mp,
		u64 max_len, gfs2_metadata_walker walker)
{
	struct gfs2_inode *ip = GFS2_I(inode);
	struct gfs2_sbd *sdp = GFS2_SB(inode);
	u64 factor = 1;
	unsigned int hgt;
	int ret;

	/*
	 * The walk starts in the lowest allocated indirect block, which may be
	 * before the position indicated by @mp.  Adjust @max_len accordingly
	 * to avoid a short walk.
	 */
	for (hgt = mp->mp_fheight - 1; hgt >= mp->mp_aheight; hgt--) {
		max_len += mp->mp_list[hgt] * factor;
		mp->mp_list[hgt] = 0;
		factor *= sdp->sd_inptrs;
	}

	for (;;) {
		u16 start = mp->mp_list[hgt];
		enum walker_status status;
		unsigned int ptrs;
		u64 len;

		/* Walk indirect block. */
		ptrs = (hgt >= 1 ? sdp->sd_inptrs : sdp->sd_diptrs) - start;
		len = ptrs * factor;
		if (len > max_len)
			ptrs = DIV_ROUND_UP_ULL(max_len, factor);
		status = walker(mp, ptrs);
		switch (status) {
		case WALK_STOP:
			return 1;
		case WALK_FOLLOW:
			BUG_ON(mp->mp_aheight == mp->mp_fheight);
			ptrs = mp->mp_list[hgt] - start;
			len = ptrs * factor;
			break;
		case WALK_CONTINUE:
			break;
		}
		if (len >= max_len)
			break;
		max_len -= len;
		if (status == WALK_FOLLOW)
			goto fill_up_metapath;

lower_metapath:
		/* Decrease height of metapath. */
		brelse(mp->mp_bh[hgt]);
		mp->mp_bh[hgt] = NULL;
		mp->mp_list[hgt] = 0;
		if (!hgt)
			break;
		hgt--;
		factor *= sdp->sd_inptrs;

		/* Advance in metadata tree. */
		(mp->mp_list[hgt])++;
		if (hgt) {
			if (mp->mp_list[hgt] >= sdp->sd_inptrs)
				goto lower_metapath;
		} else {
			if (mp->mp_list[hgt] >= sdp->sd_diptrs)
				break;
		}

fill_up_metapath:
		/* Increase height of metapath. */
		ret = fillup_metapath(ip, mp, ip->i_height - 1);
		if (ret < 0)
			return ret;
		hgt += ret;
		for (; ret; ret--)
			do_div(factor, sdp->sd_inptrs);
		mp->mp_aheight = hgt + 1;
	}
	return 0;
}

static enum walker_status gfs2_hole_walker(struct metapath *mp,
					   unsigned int ptrs)
{
	const __be64 *start, *ptr, *end;
	unsigned int hgt;

	hgt = mp->mp_aheight - 1;
	start = metapointer(hgt, mp);
	end = start + ptrs;

	for (ptr = start; ptr < end; ptr++) {
		if (*ptr) {
			mp->mp_list[hgt] += ptr - start;
			if (mp->mp_aheight == mp->mp_fheight)
				return WALK_STOP;
			return WALK_FOLLOW;
		}
	}
	return WALK_CONTINUE;
}

/**
 * gfs2_hole_size - figure out the size of a hole
 * @inode: The inode
 * @lblock: The logical starting block number
 * @len: How far to look (in blocks)
 * @mp: The metapath at lblock
 * @iomap: The iomap to store the hole size in
 *
 * This function modifies @mp.
 *
 * Returns: errno on error
 */
static int gfs2_hole_size(struct inode *inode, sector_t lblock, u64 len,
			  struct metapath *mp, struct iomap *iomap)
{
	struct metapath clone;
	u64 hole_size;
	int ret;

	clone_metapath(&clone, mp);
	ret = gfs2_walk_metadata(inode, &clone, len, gfs2_hole_walker);
	if (ret < 0)
		goto out;

	if (ret == 1)
		hole_size = metapath_to_block(GFS2_SB(inode), &clone) - lblock;
	else
		hole_size = len;
	iomap->length = hole_size << inode->i_blkbits;
	ret = 0;

out:
	release_metapath(&clone);
	return ret;
}

static inline void gfs2_indirect_init(struct metapath *mp,
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
}

enum alloc_state {
	ALLOC_DATA = 0,
	ALLOC_GROW_DEPTH = 1,
	ALLOC_GROW_HEIGHT = 2,
	/* ALLOC_UNSTUFF = 3,   TBD and rather complicated */
};

/**
 * __gfs2_iomap_alloc - Build a metadata tree of the requested height
 * @inode: The GFS2 inode
 * @iomap: The iomap structure
 * @mp: The metapath, with proper height information calculated
 *
 * In this routine we may have to alloc:
 *   i) Indirect blocks to grow the metadata tree height
 *  ii) Indirect blocks to fill in lower part of the metadata tree
 * iii) Data blocks
 *
 * This function is called after __gfs2_iomap_get, which works out the
 * total number of blocks which we need via gfs2_alloc_size.
 *
 * We then do the actual allocation asking for an extent at a time (if
 * enough contiguous free blocks are available, there will only be one
 * allocation request per call) and uses the state machine to initialise
 * the blocks in order.
 *
 * Right now, this function will allocate at most one indirect block
 * worth of data -- with a default block size of 4K, that's slightly
 * less than 2M.  If this limitation is ever removed to allow huge
 * allocations, we would probably still want to limit the iomap size we
 * return to avoid stalling other tasks during huge writes; the next
 * iomap iteration would then find the blocks already allocated.
 *
 * Returns: errno on error
 */

static int __gfs2_iomap_alloc(struct inode *inode, struct iomap *iomap,
			      struct metapath *mp)
{
	struct gfs2_inode *ip = GFS2_I(inode);
	struct gfs2_sbd *sdp = GFS2_SB(inode);
	struct buffer_head *dibh = metapath_dibh(mp);
	u64 bn;
	unsigned n, i, blks, alloced = 0, iblks = 0, branch_start = 0;
	size_t dblks = iomap->length >> inode->i_blkbits;
	const unsigned end_of_metadata = mp->mp_fheight - 1;
	int ret;
	enum alloc_state state;
	__be64 *ptr;
	__be64 zero_bn = 0;

	BUG_ON(mp->mp_aheight < 1);
	BUG_ON(dibh == NULL);
	BUG_ON(dblks < 1);

	gfs2_trans_add_meta(ip->i_gl, dibh);

	down_write(&ip->i_rw_mutex);

	if (mp->mp_fheight == mp->mp_aheight) {
		/* Bottom indirect block exists */
		state = ALLOC_DATA;
	} else {
		/* Need to allocate indirect blocks */
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
		n = blks - alloced;
		ret = gfs2_alloc_blocks(ip, &bn, &n, 0);
		if (ret)
			goto out;
		alloced += n;
		if (state != ALLOC_DATA || gfs2_is_jdata(ip))
			gfs2_trans_remove_revoke(sdp, bn, n);
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
			fallthrough;	/* To branching from existing tree */
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
			fallthrough;	/* To tree complete, adding data blocks */
		case ALLOC_DATA:
			BUG_ON(n > dblks);
			BUG_ON(mp->mp_bh[end_of_metadata] == NULL);
			gfs2_trans_add_meta(ip->i_gl, mp->mp_bh[end_of_metadata]);
			dblks = n;
			ptr = metapointer(end_of_metadata, mp);
			iomap->addr = bn << inode->i_blkbits;
			iomap->flags |= IOMAP_F_MERGED | IOMAP_F_NEW;
			while (n-- > 0)
				*ptr++ = cpu_to_be64(bn++);
			break;
		}
	} while (iomap->addr == IOMAP_NULL_ADDR);

	iomap->type = IOMAP_MAPPED;
	iomap->length = (u64)dblks << inode->i_blkbits;
	ip->i_height = mp->mp_fheight;
	gfs2_add_inode_blocks(&ip->i_inode, alloced);
	gfs2_dinode_out(ip, dibh->b_data);
out:
	up_write(&ip->i_rw_mutex);
	return ret;
}

#define IOMAP_F_GFS2_BOUNDARY IOMAP_F_PRIVATE

/**
 * gfs2_alloc_size - Compute the maximum allocation size
 * @inode: The inode
 * @mp: The metapath
 * @size: Requested size in blocks
 *
 * Compute the maximum size of the next allocation at @mp.
 *
 * Returns: size in blocks
 */
static u64 gfs2_alloc_size(struct inode *inode, struct metapath *mp, u64 size)
{
	struct gfs2_inode *ip = GFS2_I(inode);
	struct gfs2_sbd *sdp = GFS2_SB(inode);
	const __be64 *first, *ptr, *end;

	/*
	 * For writes to stuffed files, this function is called twice via
	 * __gfs2_iomap_get, before and after unstuffing. The size we return the
	 * first time needs to be large enough to get the reservation and
	 * allocation sizes right.  The size we return the second time must
	 * be exact or else __gfs2_iomap_alloc won't do the right thing.
	 */

	if (gfs2_is_stuffed(ip) || mp->mp_fheight != mp->mp_aheight) {
		unsigned int maxsize = mp->mp_fheight > 1 ?
			sdp->sd_inptrs : sdp->sd_diptrs;
		maxsize -= mp->mp_list[mp->mp_fheight - 1];
		if (size > maxsize)
			size = maxsize;
		return size;
	}

	first = metapointer(ip->i_height - 1, mp);
	end = metaend(ip->i_height - 1, mp);
	if (end - first > size)
		end = first + size;
	for (ptr = first; ptr < end; ptr++) {
		if (*ptr)
			break;
	}
	return ptr - first;
}

/**
 * __gfs2_iomap_get - Map blocks from an inode to disk blocks
 * @inode: The inode
 * @pos: Starting position in bytes
 * @length: Length to map, in bytes
 * @flags: iomap flags
 * @iomap: The iomap structure
 * @mp: The metapath
 *
 * Returns: errno
 */
static int __gfs2_iomap_get(struct inode *inode, loff_t pos, loff_t length,
			    unsigned flags, struct iomap *iomap,
			    struct metapath *mp)
{
	struct gfs2_inode *ip = GFS2_I(inode);
	struct gfs2_sbd *sdp = GFS2_SB(inode);
	loff_t size = i_size_read(inode);
	__be64 *ptr;
	sector_t lblock;
	sector_t lblock_stop;
	int ret;
	int eob;
	u64 len;
	struct buffer_head *dibh = NULL, *bh;
	u8 height;

	if (!length)
		return -EINVAL;

	down_read(&ip->i_rw_mutex);

	ret = gfs2_meta_inode_buffer(ip, &dibh);
	if (ret)
		goto unlock;
	mp->mp_bh[0] = dibh;

	if (gfs2_is_stuffed(ip)) {
		if (flags & IOMAP_WRITE) {
			loff_t max_size = gfs2_max_stuffed_size(ip);

			if (pos + length > max_size)
				goto unstuff;
			iomap->length = max_size;
		} else {
			if (pos >= size) {
				if (flags & IOMAP_REPORT) {
					ret = -ENOENT;
					goto unlock;
				} else {
					iomap->offset = pos;
					iomap->length = length;
					goto hole_found;
				}
			}
			iomap->length = size;
		}
		iomap->addr = (ip->i_no_addr << inode->i_blkbits) +
			      sizeof(struct gfs2_dinode);
		iomap->type = IOMAP_INLINE;
		iomap->inline_data = dibh->b_data + sizeof(struct gfs2_dinode);
		goto out;
	}

unstuff:
	lblock = pos >> inode->i_blkbits;
	iomap->offset = lblock << inode->i_blkbits;
	lblock_stop = (pos + length - 1) >> inode->i_blkbits;
	len = lblock_stop - lblock + 1;
	iomap->length = len << inode->i_blkbits;

	height = ip->i_height;
	while ((lblock + 1) * sdp->sd_sb.sb_bsize > sdp->sd_heightsize[height])
		height++;
	find_metapath(sdp, lblock, mp, height);
	if (height > ip->i_height || gfs2_is_stuffed(ip))
		goto do_alloc;

	ret = lookup_metapath(ip, mp);
	if (ret)
		goto unlock;

	if (mp->mp_aheight != ip->i_height)
		goto do_alloc;

	ptr = metapointer(ip->i_height - 1, mp);
	if (*ptr == 0)
		goto do_alloc;

	bh = mp->mp_bh[ip->i_height - 1];
	len = gfs2_extent_length(bh, ptr, &eob);

	iomap->addr = be64_to_cpu(*ptr) << inode->i_blkbits;
	iomap->length = len << inode->i_blkbits;
	iomap->type = IOMAP_MAPPED;
	iomap->flags |= IOMAP_F_MERGED;
	if (eob)
		iomap->flags |= IOMAP_F_GFS2_BOUNDARY;

out:
	iomap->bdev = inode->i_sb->s_bdev;
unlock:
	up_read(&ip->i_rw_mutex);
	return ret;

do_alloc:
	if (flags & IOMAP_REPORT) {
		if (pos >= size)
			ret = -ENOENT;
		else if (height == ip->i_height)
			ret = gfs2_hole_size(inode, lblock, len, mp, iomap);
		else
			iomap->length = size - iomap->offset;
	} else if (flags & IOMAP_WRITE) {
		u64 alloc_size;

		if (flags & IOMAP_DIRECT)
			goto out;  /* (see gfs2_file_direct_write) */

		len = gfs2_alloc_size(inode, mp, len);
		alloc_size = len << inode->i_blkbits;
		if (alloc_size < iomap->length)
			iomap->length = alloc_size;
	} else {
		if (pos < size && height == ip->i_height)
			ret = gfs2_hole_size(inode, lblock, len, mp, iomap);
	}
hole_found:
	iomap->addr = IOMAP_NULL_ADDR;
	iomap->type = IOMAP_HOLE;
	goto out;
}

static struct folio *
gfs2_iomap_get_folio(struct iomap_iter *iter, loff_t pos, unsigned len)
{
	struct inode *inode = iter->inode;
	unsigned int blockmask = i_blocksize(inode) - 1;
	struct gfs2_sbd *sdp = GFS2_SB(inode);
	unsigned int blocks;
	struct folio *folio;
	int status;

	blocks = ((pos & blockmask) + len + blockmask) >> inode->i_blkbits;
	status = gfs2_trans_begin(sdp, RES_DINODE + blocks, 0);
	if (status)
		return ERR_PTR(status);

	folio = iomap_get_folio(iter, pos, len);
	if (IS_ERR(folio))
		gfs2_trans_end(sdp);
	return folio;
}

static void gfs2_iomap_put_folio(struct inode *inode, loff_t pos,
				 unsigned copied, struct folio *folio)
{
	struct gfs2_trans *tr = current->journal_info;
	struct gfs2_inode *ip = GFS2_I(inode);
	struct gfs2_sbd *sdp = GFS2_SB(inode);

	if (!gfs2_is_stuffed(ip))
		gfs2_trans_add_databufs(ip, folio, offset_in_folio(folio, pos),
					copied);

	folio_unlock(folio);
	folio_put(folio);

	if (tr->tr_num_buf_new)
		__mark_inode_dirty(inode, I_DIRTY_DATASYNC);

	gfs2_trans_end(sdp);
}

static const struct iomap_folio_ops gfs2_iomap_folio_ops = {
	.get_folio = gfs2_iomap_get_folio,
	.put_folio = gfs2_iomap_put_folio,
};

static int gfs2_iomap_begin_write(struct inode *inode, loff_t pos,
				  loff_t length, unsigned flags,
				  struct iomap *iomap,
				  struct metapath *mp)
{
	struct gfs2_inode *ip = GFS2_I(inode);
	struct gfs2_sbd *sdp = GFS2_SB(inode);
	bool unstuff;
	int ret;

	unstuff = gfs2_is_stuffed(ip) &&
		  pos + length > gfs2_max_stuffed_size(ip);

	if (unstuff || iomap->type == IOMAP_HOLE) {
		unsigned int data_blocks, ind_blocks;
		struct gfs2_alloc_parms ap = {};
		unsigned int rblocks;
		struct gfs2_trans *tr;

		gfs2_write_calc_reserv(ip, iomap->length, &data_blocks,
				       &ind_blocks);
		ap.target = data_blocks + ind_blocks;
		ret = gfs2_quota_lock_check(ip, &ap);
		if (ret)
			return ret;

		ret = gfs2_inplace_reserve(ip, &ap);
		if (ret)
			goto out_qunlock;

		rblocks = RES_DINODE + ind_blocks;
		if (gfs2_is_jdata(ip))
			rblocks += data_blocks;
		if (ind_blocks || data_blocks)
			rblocks += RES_STATFS + RES_QUOTA;
		if (inode == sdp->sd_rindex)
			rblocks += 2 * RES_STATFS;
		rblocks += gfs2_rg_blocks(ip, data_blocks + ind_blocks);

		ret = gfs2_trans_begin(sdp, rblocks,
				       iomap->length >> inode->i_blkbits);
		if (ret)
			goto out_trans_fail;

		if (unstuff) {
			ret = gfs2_unstuff_dinode(ip);
			if (ret)
				goto out_trans_end;
			release_metapath(mp);
			ret = __gfs2_iomap_get(inode, iomap->offset,
					       iomap->length, flags, iomap, mp);
			if (ret)
				goto out_trans_end;
		}

		if (iomap->type == IOMAP_HOLE) {
			ret = __gfs2_iomap_alloc(inode, iomap, mp);
			if (ret) {
				gfs2_trans_end(sdp);
				gfs2_inplace_release(ip);
				punch_hole(ip, iomap->offset, iomap->length);
				goto out_qunlock;
			}
		}

		tr = current->journal_info;
		if (tr->tr_num_buf_new)
			__mark_inode_dirty(inode, I_DIRTY_DATASYNC);

		gfs2_trans_end(sdp);
	}

	if (gfs2_is_stuffed(ip) || gfs2_is_jdata(ip))
		iomap->folio_ops = &gfs2_iomap_folio_ops;
	return 0;

out_trans_end:
	gfs2_trans_end(sdp);
out_trans_fail:
	gfs2_inplace_release(ip);
out_qunlock:
	gfs2_quota_unlock(ip);
	return ret;
}

static int gfs2_iomap_begin(struct inode *inode, loff_t pos, loff_t length,
			    unsigned flags, struct iomap *iomap,
			    struct iomap *srcmap)
{
	struct gfs2_inode *ip = GFS2_I(inode);
	struct metapath mp = { .mp_aheight = 1, };
	int ret;

	if (gfs2_is_jdata(ip))
		iomap->flags |= IOMAP_F_BUFFER_HEAD;

	trace_gfs2_iomap_start(ip, pos, length, flags);
	ret = __gfs2_iomap_get(inode, pos, length, flags, iomap, &mp);
	if (ret)
		goto out_unlock;

	switch(flags & (IOMAP_WRITE | IOMAP_ZERO)) {
	case IOMAP_WRITE:
		if (flags & IOMAP_DIRECT) {
			/*
			 * Silently fall back to buffered I/O for stuffed files
			 * or if we've got a hole (see gfs2_file_direct_write).
			 */
			if (iomap->type != IOMAP_MAPPED)
				ret = -ENOTBLK;
			goto out_unlock;
		}
		break;
	case IOMAP_ZERO:
		if (iomap->type == IOMAP_HOLE)
			goto out_unlock;
		break;
	default:
		goto out_unlock;
	}

	ret = gfs2_iomap_begin_write(inode, pos, length, flags, iomap, &mp);

out_unlock:
	release_metapath(&mp);
	trace_gfs2_iomap_end(ip, iomap, ret);
	return ret;
}

static int gfs2_iomap_end(struct inode *inode, loff_t pos, loff_t length,
			  ssize_t written, unsigned flags, struct iomap *iomap)
{
	struct gfs2_inode *ip = GFS2_I(inode);
	struct gfs2_sbd *sdp = GFS2_SB(inode);

	switch (flags & (IOMAP_WRITE | IOMAP_ZERO)) {
	case IOMAP_WRITE:
		if (flags & IOMAP_DIRECT)
			return 0;
		break;
	case IOMAP_ZERO:
		 if (iomap->type == IOMAP_HOLE)
			 return 0;
		 break;
	default:
		 return 0;
	}

	if (!gfs2_is_stuffed(ip))
		gfs2_ordered_add_inode(ip);

	if (inode == sdp->sd_rindex)
		adjust_fs_space(inode);

	gfs2_inplace_release(ip);

	if (ip->i_qadata && ip->i_qadata->qa_qd_num)
		gfs2_quota_unlock(ip);

	if (length != written && (iomap->flags & IOMAP_F_NEW)) {
		/* Deallocate blocks that were just allocated. */
		loff_t hstart = round_up(pos + written, i_blocksize(inode));
		loff_t hend = iomap->offset + iomap->length;

		if (hstart < hend) {
			truncate_pagecache_range(inode, hstart, hend - 1);
			punch_hole(ip, hstart, hend - hstart);
		}
	}

	if (unlikely(!written))
		return 0;

	if (iomap->flags & IOMAP_F_SIZE_CHANGED)
		mark_inode_dirty(inode);
	set_bit(GLF_DIRTY, &ip->i_gl->gl_flags);
	return 0;
}

const struct iomap_ops gfs2_iomap_ops = {
	.iomap_begin = gfs2_iomap_begin,
	.iomap_end = gfs2_iomap_end,
};

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
	loff_t pos = (loff_t)lblock << inode->i_blkbits;
	loff_t length = bh_map->b_size;
	struct iomap iomap = { };
	int ret;

	clear_buffer_mapped(bh_map);
	clear_buffer_new(bh_map);
	clear_buffer_boundary(bh_map);
	trace_gfs2_bmap(ip, bh_map, lblock, create, 1);

	if (!create)
		ret = gfs2_iomap_get(inode, pos, length, &iomap);
	else
		ret = gfs2_iomap_alloc(inode, pos, length, &iomap);
	if (ret)
		goto out;

	if (iomap.length > bh_map->b_size) {
		iomap.length = bh_map->b_size;
		iomap.flags &= ~IOMAP_F_GFS2_BOUNDARY;
	}
	if (iomap.addr != IOMAP_NULL_ADDR)
		map_bh(bh_map, inode->i_sb, iomap.addr >> inode->i_blkbits);
	bh_map->b_size = iomap.length;
	if (iomap.flags & IOMAP_F_GFS2_BOUNDARY)
		set_buffer_boundary(bh_map);
	if (iomap.flags & IOMAP_F_NEW)
		set_buffer_new(bh_map);

out:
	trace_gfs2_bmap(ip, bh_map, lblock, create, ret);
	return ret;
}

int gfs2_get_extent(struct inode *inode, u64 lblock, u64 *dblock,
		    unsigned int *extlen)
{
	unsigned int blkbits = inode->i_blkbits;
	struct iomap iomap = { };
	unsigned int len;
	int ret;

	ret = gfs2_iomap_get(inode, lblock << blkbits, *extlen << blkbits,
			     &iomap);
	if (ret)
		return ret;
	if (iomap.type != IOMAP_MAPPED)
		return -EIO;
	*dblock = iomap.addr >> blkbits;
	len = iomap.length >> blkbits;
	if (len < *extlen)
		*extlen = len;
	return 0;
}

int gfs2_alloc_extent(struct inode *inode, u64 lblock, u64 *dblock,
		      unsigned int *extlen, bool *new)
{
	unsigned int blkbits = inode->i_blkbits;
	struct iomap iomap = { };
	unsigned int len;
	int ret;

	ret = gfs2_iomap_alloc(inode, lblock << blkbits, *extlen << blkbits,
			       &iomap);
	if (ret)
		return ret;
	if (iomap.type != IOMAP_MAPPED)
		return -EIO;
	*dblock = iomap.addr >> blkbits;
	len = iomap.length >> blkbits;
	if (len < *extlen)
		*extlen = len;
	*new = iomap.flags & IOMAP_F_NEW;
	return 0;
}

/*
 * NOTE: Never call gfs2_block_zero_range with an open transaction because it
 * uses iomap write to perform its actions, which begin their own transactions
 * (iomap_begin, get_folio, etc.)
 */
static int gfs2_block_zero_range(struct inode *inode, loff_t from,
				 unsigned int length)
{
	BUG_ON(current->journal_info);
	return iomap_zero_range(inode, from, length, NULL, &gfs2_iomap_ops,
			NULL);
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

	if (!gfs2_is_stuffed(ip)) {
		unsigned int blocksize = i_blocksize(inode);
		unsigned int offs = newsize & (blocksize - 1);
		if (offs) {
			error = gfs2_block_zero_range(inode, newsize,
						      blocksize - offs);
			if (error)
				return error;
		}
	}
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

	if (gfs2_is_stuffed(ip))
		gfs2_buffer_clear_tail(dibh, sizeof(struct gfs2_dinode) + newsize);
	else
		ip->i_diskflags |= GFS2_DIF_TRUNC_IN_PROG;

	i_size_write(inode, newsize);
	inode_set_mtime_to_ts(&ip->i_inode, inode_set_ctime_current(&ip->i_inode));
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

int gfs2_iomap_get(struct inode *inode, loff_t pos, loff_t length,
		   struct iomap *iomap)
{
	struct metapath mp = { .mp_aheight = 1, };
	int ret;

	ret = __gfs2_iomap_get(inode, pos, length, 0, iomap, &mp);
	release_metapath(&mp);
	return ret;
}

int gfs2_iomap_alloc(struct inode *inode, loff_t pos, loff_t length,
		     struct iomap *iomap)
{
	struct metapath mp = { .mp_aheight = 1, };
	int ret;

	ret = __gfs2_iomap_get(inode, pos, length, IOMAP_WRITE, iomap, &mp);
	if (!ret && iomap->type == IOMAP_HOLE)
		ret = __gfs2_iomap_alloc(inode, iomap, &mp);
	release_metapath(&mp);
	return ret;
}

/**
 * sweep_bh_for_rgrps - find an rgrp in a meta buffer and free blocks therein
 * @ip: inode
 * @rd_gh: holder of resource group glock
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
						 LM_FLAG_NODE_SCOPE, rd_gh);
			if (ret)
				goto out;

			/* Must be done with the rgrp glock held: */
			if (gfs2_rs_active(&ip->i_res) &&
			    rgd == ip->i_res.rs_rgd)
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
			__gfs2_free_blocks(ip, rgd, bstart, (u32)blen, meta);
			(*btotal) += blen;
			gfs2_add_inode_blocks(&ip->i_inode, -blen);
		}
		bstart = bn;
		blen = 1;
	}
	if (bstart) {
		__gfs2_free_blocks(ip, rgd, bstart, (u32)blen, meta);
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
			inode_set_mtime_to_ts(&ip->i_inode, inode_set_ctime_current(&ip->i_inode));
			gfs2_trans_add_meta(ip->i_gl, dibh);
			gfs2_dinode_out(ip, dibh->b_data);
			brelse(dibh);
			up_write(&ip->i_rw_mutex);
			gfs2_trans_end(sdp);
			buf_in_tr = false;
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
 * @sdp: The superblock
 * @mp: starting metapath
 * @h: desired height to search
 * @end_list: See punch_hole().
 * @end_aligned: See punch_hole().
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
	unsigned int bsize = 1 << bsize_shift;
	u64 lblock = (offset + bsize - 1) >> bsize_shift;
	__u16 start_list[GFS2_MAX_META_HEIGHT];
	__u16 __end_list[GFS2_MAX_META_HEIGHT], *end_list = NULL;
	unsigned int start_aligned, end_aligned;
	unsigned int strip_h = ip->i_height - 1;
	u32 btotal = 0;
	int ret, state;
	int mp_h; /* metapath buffers are read in to this height */
	u64 prev_bnr = 0;
	__be64 *start, *end;

	if (offset + bsize - 1 >= maxsize) {
		/*
		 * The starting point lies beyond the allocated metadata;
		 * there are no blocks to deallocate.
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
				fs_emerg(sdp, "inode %llu, block:%llu, i_h:%u, "
					 "s_h:%u, mp_h:%u\n",
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

			/* On the first pass, issue read-ahead on metadata. */
			if (mp.mp_aheight > 1 && strip_h == ip->i_height - 1) {
				unsigned int height = mp.mp_aheight - 1;

				/* No read-ahead for data blocks. */
				if (mp.mp_aheight - 1 == strip_h)
					height--;

				for (; height >= mp.mp_aheight - ret; height--) {
					metapointer_range(&mp, height,
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
		inode_set_mtime_to_ts(&ip->i_inode, inode_set_ctime_current(&ip->i_inode));
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
	inode_set_mtime_to_ts(&ip->i_inode, inode_set_ctime_current(&ip->i_inode));
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
				 (unstuff &&
				  gfs2_is_jdata(ip) ? RES_JDATA : 0) +
				 (sdp->sd_args.ar_quota == GFS2_QUOTA_OFF ?
				  0 : RES_QUOTA), 0);
	if (error)
		goto do_grow_release;

	if (unstuff) {
		error = gfs2_unstuff_dinode(ip);
		if (error)
			goto do_end_trans;
	}

	error = gfs2_meta_inode_buffer(ip, &dibh);
	if (error)
		goto do_end_trans;

	truncate_setsize(inode, size);
	inode_set_mtime_to_ts(&ip->i_inode, inode_set_ctime_current(&ip->i_inode));
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

	ret = gfs2_qa_get(ip);
	if (ret)
		goto out;

	if (newsize >= inode->i_size) {
		ret = do_grow(inode, newsize);
		goto out;
	}

	ret = do_shrink(inode, newsize);
out:
	gfs2_rs_delete(ip);
	gfs2_qa_put(ip);
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
		jext = list_first_entry(&jd->extent_list, struct gfs2_journal_extent, list);
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
		jext = list_last_entry(&jd->extent_list, struct gfs2_journal_extent, list);
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
	ktime_t start, end;

	start = ktime_get();
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

	end = ktime_get();
	fs_info(sdp, "journal %d mapped with %u extents in %lldms\n", jd->jd_jid,
		jd->nr_extents, ktime_ms_delta(end, start));
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
	if (lblock_stop > end_of_file && ip != GFS2_I(sdp->sd_rindex))
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
	unsigned int blocksize = i_blocksize(inode);
	loff_t start, end;
	int error;

	if (!gfs2_is_stuffed(ip)) {
		unsigned int start_off, end_len;

		start_off = offset & (blocksize - 1);
		end_len = (offset + length) & (blocksize - 1);
		if (start_off) {
			unsigned int len = length;
			if (length > blocksize - start_off)
				len = blocksize - start_off;
			error = gfs2_block_zero_range(inode, offset, len);
			if (error)
				goto out;
			if (start_off + length < blocksize)
				end_len = 0;
		}
		if (end_len) {
			error = gfs2_block_zero_range(inode,
				offset + length - end_len, end_len);
			if (error)
				goto out;
		}
	}

	start = round_down(offset, blocksize);
	end = round_up(offset + length, blocksize) - 1;
	error = filemap_write_and_wait_range(inode->i_mapping, start, end);
	if (error)
		return error;

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

static int gfs2_map_blocks(struct iomap_writepage_ctx *wpc, struct inode *inode,
		loff_t offset, unsigned int len)
{
	int ret;

	if (WARN_ON_ONCE(gfs2_is_stuffed(GFS2_I(inode))))
		return -EIO;

	if (offset >= wpc->iomap.offset &&
	    offset < wpc->iomap.offset + wpc->iomap.length)
		return 0;

	memset(&wpc->iomap, 0, sizeof(wpc->iomap));
	ret = gfs2_iomap_get(inode, offset, INT_MAX, &wpc->iomap);
	return ret;
}

const struct iomap_writeback_ops gfs2_writeback_ops = {
	.map_blocks		= gfs2_map_blocks,
};
