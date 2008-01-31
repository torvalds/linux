/*
 * Copyright (C) Sistina Software, Inc.  1997-2003 All rights reserved.
 * Copyright (C) 2004-2006 Red Hat, Inc.  All rights reserved.
 *
 * This copyrighted material is made available to anyone wishing to use,
 * modify, copy, or redistribute it subject to the terms and conditions
 * of the GNU General Public License version 2.
 */

#include <linux/slab.h>
#include <linux/spinlock.h>
#include <linux/completion.h>
#include <linux/buffer_head.h>
#include <linux/gfs2_ondisk.h>
#include <linux/crc32.h>
#include <linux/lm_interface.h>

#include "gfs2.h"
#include "incore.h"
#include "bmap.h"
#include "glock.h"
#include "inode.h"
#include "meta_io.h"
#include "quota.h"
#include "rgrp.h"
#include "trans.h"
#include "dir.h"
#include "util.h"
#include "ops_address.h"

/* This doesn't need to be that large as max 64 bit pointers in a 4k
 * block is 512, so __u16 is fine for that. It saves stack space to
 * keep it small.
 */
struct metapath {
	__u16 mp_list[GFS2_MAX_META_HEIGHT];
};

typedef int (*block_call_t) (struct gfs2_inode *ip, struct buffer_head *dibh,
			     struct buffer_head *bh, __be64 *top,
			     __be64 *bottom, unsigned int height,
			     void *data);

struct strip_mine {
	int sm_first;
	unsigned int sm_height;
};

/**
 * gfs2_unstuffer_page - unstuff a stuffed inode into a block cached by a page
 * @ip: the inode
 * @dibh: the dinode buffer
 * @block: the block number that was allocated
 * @private: any locked page held by the caller process
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
		page = grab_cache_page(inode->i_mapping, 0);
		if (!page)
			return -ENOMEM;
		release = 1;
	}

	if (!PageUptodate(page)) {
		void *kaddr = kmap(page);

		memcpy(kaddr, dibh->b_data + sizeof(struct gfs2_dinode),
		       ip->i_di.di_size);
		memset(kaddr + ip->i_di.di_size, 0,
		       PAGE_CACHE_SIZE - ip->i_di.di_size);
		kunmap(page);

		SetPageUptodate(page);
	}

	if (!page_has_buffers(page))
		create_empty_buffers(page, 1 << inode->i_blkbits,
				     (1 << BH_Uptodate));

	bh = page_buffers(page);

	if (!buffer_mapped(bh))
		map_bh(bh, inode->i_sb, block);

	set_buffer_uptodate(bh);
	if (!gfs2_is_jdata(ip))
		mark_buffer_dirty(bh);
	if (!gfs2_is_writeback(ip))
		gfs2_trans_add_bh(ip->i_gl, bh, 0);

	if (release) {
		unlock_page(page);
		page_cache_release(page);
	}

	return 0;
}

/**
 * gfs2_unstuff_dinode - Unstuff a dinode when the data has grown too big
 * @ip: The GFS2 inode to unstuff
 * @unstuffer: the routine that handles unstuffing a non-zero length file
 * @private: private data for the unstuffer
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

	if (ip->i_di.di_size) {
		/* Get a free block, fill it with the stuffed data,
		   and write it out to disk */

		if (isdir) {
			block = gfs2_alloc_meta(ip);

			error = gfs2_dir_get_new_buffer(ip, block, &bh);
			if (error)
				goto out_brelse;
			gfs2_buffer_copy_tail(bh, sizeof(struct gfs2_meta_header),
					      dibh, sizeof(struct gfs2_dinode));
			brelse(bh);
		} else {
			block = gfs2_alloc_data(ip);

			error = gfs2_unstuffer_page(ip, dibh, block, page);
			if (error)
				goto out_brelse;
		}
	}

	/*  Set up the pointer to the new block  */

	gfs2_trans_add_bh(ip->i_gl, dibh, 1);
	di = (struct gfs2_dinode *)dibh->b_data;
	gfs2_buffer_clear_tail(dibh, sizeof(struct gfs2_dinode));

	if (ip->i_di.di_size) {
		*(__be64 *)(di + 1) = cpu_to_be64(block);
		ip->i_di.di_blocks++;
		gfs2_set_inode_blocks(&ip->i_inode);
		di->di_blocks = cpu_to_be64(ip->i_di.di_blocks);
	}

	ip->i_di.di_height = 1;
	di->di_height = cpu_to_be16(1);

out_brelse:
	brelse(dibh);
out:
	up_write(&ip->i_rw_mutex);
	return error;
}

/**
 * calc_tree_height - Calculate the height of a metadata tree
 * @ip: The GFS2 inode
 * @size: The proposed size of the file
 *
 * Work out how tall a metadata tree needs to be in order to accommodate a
 * file of a particular size. If size is less than the current size of
 * the inode, then the current size of the inode is used instead of the
 * supplied one.
 *
 * Returns: the height the tree should be
 */

static unsigned int calc_tree_height(struct gfs2_inode *ip, u64 size)
{
	struct gfs2_sbd *sdp = GFS2_SB(&ip->i_inode);
	u64 *arr;
	unsigned int max, height;

	if (ip->i_di.di_size > size)
		size = ip->i_di.di_size;

	if (gfs2_is_dir(ip)) {
		arr = sdp->sd_jheightsize;
		max = sdp->sd_max_jheight;
	} else {
		arr = sdp->sd_heightsize;
		max = sdp->sd_max_height;
	}

	for (height = 0; height < max; height++)
		if (arr[height] >= size)
			break;

	return height;
}

/**
 * build_height - Build a metadata tree of the requested height
 * @ip: The GFS2 inode
 * @height: The height to build to
 *
 *
 * Returns: errno
 */

static int build_height(struct inode *inode, unsigned height)
{
	struct gfs2_inode *ip = GFS2_I(inode);
	unsigned new_height = height - ip->i_di.di_height;
	struct buffer_head *dibh;
	struct buffer_head *blocks[GFS2_MAX_META_HEIGHT];
	struct gfs2_dinode *di;
	int error;
	__be64 *bp;
	u64 bn;
	unsigned n;

	if (height <= ip->i_di.di_height)
		return 0;

	error = gfs2_meta_inode_buffer(ip, &dibh);
	if (error)
		return error;

	for(n = 0; n < new_height; n++) {
		bn = gfs2_alloc_meta(ip);
		blocks[n] = gfs2_meta_new(ip->i_gl, bn);
		gfs2_trans_add_bh(ip->i_gl, blocks[n], 1);
	}

	n = 0;
	bn = blocks[0]->b_blocknr;
	if (new_height > 1) {
		for(; n < new_height-1; n++) {
			gfs2_metatype_set(blocks[n], GFS2_METATYPE_IN,
					  GFS2_FORMAT_IN);
			gfs2_buffer_clear_tail(blocks[n],
					       sizeof(struct gfs2_meta_header));
			bp = (__be64 *)(blocks[n]->b_data +
				     sizeof(struct gfs2_meta_header));
			*bp = cpu_to_be64(blocks[n+1]->b_blocknr);
			brelse(blocks[n]);
			blocks[n] = NULL;
		}
	}
	gfs2_metatype_set(blocks[n], GFS2_METATYPE_IN, GFS2_FORMAT_IN);
	gfs2_buffer_copy_tail(blocks[n], sizeof(struct gfs2_meta_header),
			      dibh, sizeof(struct gfs2_dinode));
	brelse(blocks[n]);
	gfs2_trans_add_bh(ip->i_gl, dibh, 1);
	di = (struct gfs2_dinode *)dibh->b_data;
	gfs2_buffer_clear_tail(dibh, sizeof(struct gfs2_dinode));
	*(__be64 *)(di + 1) = cpu_to_be64(bn);
	ip->i_di.di_height += new_height;
	ip->i_di.di_blocks += new_height;
	gfs2_set_inode_blocks(&ip->i_inode);
	di->di_height = cpu_to_be16(ip->i_di.di_height);
	di->di_blocks = cpu_to_be64(ip->i_di.di_blocks);
	brelse(dibh);
	return error;
}

/**
 * find_metapath - Find path through the metadata tree
 * @ip: The inode pointer
 * @mp: The metapath to return the result in
 * @block: The disk block to look up
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

static void find_metapath(struct gfs2_inode *ip, u64 block,
			  struct metapath *mp)
{
	struct gfs2_sbd *sdp = GFS2_SB(&ip->i_inode);
	u64 b = block;
	unsigned int i;

	for (i = ip->i_di.di_height; i--;)
		mp->mp_list[i] = do_div(b, sdp->sd_inptrs);

}

/**
 * metapointer - Return pointer to start of metadata in a buffer
 * @bh: The buffer
 * @height: The metadata height (0 = dinode)
 * @mp: The metapath
 *
 * Return a pointer to the block number of the next height of the metadata
 * tree given a buffer containing the pointer to the current height of the
 * metadata tree.
 */

static inline __be64 *metapointer(struct buffer_head *bh, int *boundary,
			       unsigned int height, const struct metapath *mp)
{
	unsigned int head_size = (height > 0) ?
		sizeof(struct gfs2_meta_header) : sizeof(struct gfs2_dinode);
	__be64 *ptr;
	*boundary = 0;
	ptr = ((__be64 *)(bh->b_data + head_size)) + mp->mp_list[height];
	if (ptr + 1 == (__be64 *)(bh->b_data + bh->b_size))
		*boundary = 1;
	return ptr;
}

/**
 * lookup_block - Get the next metadata block in metadata tree
 * @ip: The GFS2 inode
 * @bh: Buffer containing the pointers to metadata blocks
 * @height: The height of the tree (0 = dinode)
 * @mp: The metapath
 * @create: Non-zero if we may create a new meatdata block
 * @new: Used to indicate if we did create a new metadata block
 * @block: the returned disk block number
 *
 * Given a metatree, complete to a particular height, checks to see if the next
 * height of the tree exists. If not the next height of the tree is created.
 * The block number of the next height of the metadata tree is returned.
 *
 */

static int lookup_block(struct gfs2_inode *ip, struct buffer_head *bh,
			unsigned int height, struct metapath *mp, int create,
			int *new, u64 *block)
{
	int boundary;
	__be64 *ptr = metapointer(bh, &boundary, height, mp);

	if (*ptr) {
		*block = be64_to_cpu(*ptr);
		return boundary;
	}

	*block = 0;

	if (!create)
		return 0;

	if (height == ip->i_di.di_height - 1 && !gfs2_is_dir(ip))
		*block = gfs2_alloc_data(ip);
	else
		*block = gfs2_alloc_meta(ip);

	gfs2_trans_add_bh(ip->i_gl, bh, 1);

	*ptr = cpu_to_be64(*block);
	ip->i_di.di_blocks++;
	gfs2_set_inode_blocks(&ip->i_inode);

	*new = 1;
	return 0;
}

static inline void bmap_lock(struct inode *inode, int create)
{
	struct gfs2_inode *ip = GFS2_I(inode);
	if (create)
		down_write(&ip->i_rw_mutex);
	else
		down_read(&ip->i_rw_mutex);
}

static inline void bmap_unlock(struct inode *inode, int create)
{
	struct gfs2_inode *ip = GFS2_I(inode);
	if (create)
		up_write(&ip->i_rw_mutex);
	else
		up_read(&ip->i_rw_mutex);
}

/**
 * gfs2_block_map - Map a block from an inode to a disk block
 * @inode: The inode
 * @lblock: The logical block number
 * @bh_map: The bh to be mapped
 *
 * Find the block number on the current device which corresponds to an
 * inode's block. If the block had to be created, "new" will be set.
 *
 * Returns: errno
 */

int gfs2_block_map(struct inode *inode, sector_t lblock,
		   struct buffer_head *bh_map, int create)
{
	struct gfs2_inode *ip = GFS2_I(inode);
	struct gfs2_sbd *sdp = GFS2_SB(inode);
	struct buffer_head *bh;
	unsigned int bsize;
	unsigned int height;
	unsigned int end_of_metadata;
	unsigned int x;
	int error = 0;
	int new = 0;
	u64 dblock = 0;
	int boundary;
	unsigned int maxlen = bh_map->b_size >> inode->i_blkbits;
	struct metapath mp;
	u64 size;
	struct buffer_head *dibh = NULL;

	BUG_ON(maxlen == 0);

	if (gfs2_assert_warn(sdp, !gfs2_is_stuffed(ip)))
		return 0;

	bmap_lock(inode, create);
	clear_buffer_mapped(bh_map);
	clear_buffer_new(bh_map);
	clear_buffer_boundary(bh_map);
	bsize = gfs2_is_dir(ip) ? sdp->sd_jbsize : sdp->sd_sb.sb_bsize;
	size = (lblock + 1) * bsize;

	if (size > ip->i_di.di_size) {
		height = calc_tree_height(ip, size);
		if (ip->i_di.di_height < height) {
			if (!create)
				goto out_ok;
	
			error = build_height(inode, height);
			if (error)
				goto out_fail;
		}
	}

	find_metapath(ip, lblock, &mp);
	end_of_metadata = ip->i_di.di_height - 1;
	error = gfs2_meta_inode_buffer(ip, &bh);
	if (error)
		goto out_fail;
	dibh = bh;
	get_bh(dibh);

	for (x = 0; x < end_of_metadata; x++) {
		lookup_block(ip, bh, x, &mp, create, &new, &dblock);
		brelse(bh);
		if (!dblock)
			goto out_ok;

		error = gfs2_meta_indirect_buffer(ip, x+1, dblock, new, &bh);
		if (error)
			goto out_fail;
	}

	boundary = lookup_block(ip, bh, end_of_metadata, &mp, create, &new, &dblock);
	if (dblock) {
		map_bh(bh_map, inode->i_sb, dblock);
		if (boundary)
			set_buffer_boundary(bh_map);
		if (new) {
			gfs2_trans_add_bh(ip->i_gl, dibh, 1);
			gfs2_dinode_out(ip, dibh->b_data);
			set_buffer_new(bh_map);
			goto out_brelse;
		}
		while(--maxlen && !buffer_boundary(bh_map)) {
			u64 eblock;

			mp.mp_list[end_of_metadata]++;
			boundary = lookup_block(ip, bh, end_of_metadata, &mp, 0, &new, &eblock);
			if (eblock != ++dblock)
				break;
			bh_map->b_size += (1 << inode->i_blkbits);
			if (boundary)
				set_buffer_boundary(bh_map);
		}
	}
out_brelse:
	brelse(bh);
out_ok:
	error = 0;
out_fail:
	if (dibh)
		brelse(dibh);
	bmap_unlock(inode, create);
	return error;
}

int gfs2_extent_map(struct inode *inode, u64 lblock, int *new, u64 *dblock, unsigned *extlen)
{
	struct buffer_head bh = { .b_state = 0, .b_blocknr = 0 };
	int ret;
	int create = *new;

	BUG_ON(!extlen);
	BUG_ON(!dblock);
	BUG_ON(!new);

	bh.b_size = 1 << (inode->i_blkbits + 5);
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
 * recursive_scan - recursively scan through the end of a file
 * @ip: the inode
 * @dibh: the dinode buffer
 * @mp: the path through the metadata to the point to start
 * @height: the height the recursion is at
 * @block: the indirect block to look at
 * @first: 1 if this is the first block
 * @bc: the call to make for each piece of metadata
 * @data: data opaque to this function to pass to @bc
 *
 * When this is first called @height and @block should be zero and
 * @first should be 1.
 *
 * Returns: errno
 */

static int recursive_scan(struct gfs2_inode *ip, struct buffer_head *dibh,
			  struct metapath *mp, unsigned int height,
			  u64 block, int first, block_call_t bc,
			  void *data)
{
	struct gfs2_sbd *sdp = GFS2_SB(&ip->i_inode);
	struct buffer_head *bh = NULL;
	__be64 *top, *bottom;
	u64 bn;
	int error;
	int mh_size = sizeof(struct gfs2_meta_header);

	if (!height) {
		error = gfs2_meta_inode_buffer(ip, &bh);
		if (error)
			return error;
		dibh = bh;

		top = (__be64 *)(bh->b_data + sizeof(struct gfs2_dinode)) + mp->mp_list[0];
		bottom = (__be64 *)(bh->b_data + sizeof(struct gfs2_dinode)) + sdp->sd_diptrs;
	} else {
		error = gfs2_meta_indirect_buffer(ip, height, block, 0, &bh);
		if (error)
			return error;

		top = (__be64 *)(bh->b_data + mh_size) +
				  (first ? mp->mp_list[height] : 0);

		bottom = (__be64 *)(bh->b_data + mh_size) + sdp->sd_inptrs;
	}

	error = bc(ip, dibh, bh, top, bottom, height, data);
	if (error)
		goto out;

	if (height < ip->i_di.di_height - 1)
		for (; top < bottom; top++, first = 0) {
			if (!*top)
				continue;

			bn = be64_to_cpu(*top);

			error = recursive_scan(ip, dibh, mp, height + 1, bn,
					       first, bc, data);
			if (error)
				break;
		}

out:
	brelse(bh);
	return error;
}

/**
 * do_strip - Look for a layer a particular layer of the file and strip it off
 * @ip: the inode
 * @dibh: the dinode buffer
 * @bh: A buffer of pointers
 * @top: The first pointer in the buffer
 * @bottom: One more than the last pointer
 * @height: the height this buffer is at
 * @data: a pointer to a struct strip_mine
 *
 * Returns: errno
 */

static int do_strip(struct gfs2_inode *ip, struct buffer_head *dibh,
		    struct buffer_head *bh, __be64 *top, __be64 *bottom,
		    unsigned int height, void *data)
{
	struct strip_mine *sm = data;
	struct gfs2_sbd *sdp = GFS2_SB(&ip->i_inode);
	struct gfs2_rgrp_list rlist;
	u64 bn, bstart;
	u32 blen;
	__be64 *p;
	unsigned int rg_blocks = 0;
	int metadata;
	unsigned int revokes = 0;
	int x;
	int error;

	if (!*top)
		sm->sm_first = 0;

	if (height != sm->sm_height)
		return 0;

	if (sm->sm_first) {
		top++;
		sm->sm_first = 0;
	}

	metadata = (height != ip->i_di.di_height - 1);
	if (metadata)
		revokes = (height) ? sdp->sd_inptrs : sdp->sd_diptrs;

	error = gfs2_rindex_hold(sdp, &ip->i_alloc->al_ri_gh);
	if (error)
		return error;

	memset(&rlist, 0, sizeof(struct gfs2_rgrp_list));
	bstart = 0;
	blen = 0;

	for (p = top; p < bottom; p++) {
		if (!*p)
			continue;

		bn = be64_to_cpu(*p);

		if (bstart + blen == bn)
			blen++;
		else {
			if (bstart)
				gfs2_rlist_add(sdp, &rlist, bstart);

			bstart = bn;
			blen = 1;
		}
	}

	if (bstart)
		gfs2_rlist_add(sdp, &rlist, bstart);
	else
		goto out; /* Nothing to do */

	gfs2_rlist_alloc(&rlist, LM_ST_EXCLUSIVE, 0);

	for (x = 0; x < rlist.rl_rgrps; x++) {
		struct gfs2_rgrpd *rgd;
		rgd = rlist.rl_ghs[x].gh_gl->gl_object;
		rg_blocks += rgd->rd_length;
	}

	error = gfs2_glock_nq_m(rlist.rl_rgrps, rlist.rl_ghs);
	if (error)
		goto out_rlist;

	error = gfs2_trans_begin(sdp, rg_blocks + RES_DINODE +
				 RES_INDIRECT + RES_STATFS + RES_QUOTA,
				 revokes);
	if (error)
		goto out_rg_gunlock;

	down_write(&ip->i_rw_mutex);

	gfs2_trans_add_bh(ip->i_gl, dibh, 1);
	gfs2_trans_add_bh(ip->i_gl, bh, 1);

	bstart = 0;
	blen = 0;

	for (p = top; p < bottom; p++) {
		if (!*p)
			continue;

		bn = be64_to_cpu(*p);

		if (bstart + blen == bn)
			blen++;
		else {
			if (bstart) {
				if (metadata)
					gfs2_free_meta(ip, bstart, blen);
				else
					gfs2_free_data(ip, bstart, blen);
			}

			bstart = bn;
			blen = 1;
		}

		*p = 0;
		if (!ip->i_di.di_blocks)
			gfs2_consist_inode(ip);
		ip->i_di.di_blocks--;
		gfs2_set_inode_blocks(&ip->i_inode);
	}
	if (bstart) {
		if (metadata)
			gfs2_free_meta(ip, bstart, blen);
		else
			gfs2_free_data(ip, bstart, blen);
	}

	ip->i_inode.i_mtime = ip->i_inode.i_ctime = CURRENT_TIME;

	gfs2_dinode_out(ip, dibh->b_data);

	up_write(&ip->i_rw_mutex);

	gfs2_trans_end(sdp);

out_rg_gunlock:
	gfs2_glock_dq_m(rlist.rl_rgrps, rlist.rl_ghs);
out_rlist:
	gfs2_rlist_free(&rlist);
out:
	gfs2_glock_dq_uninit(&ip->i_alloc->al_ri_gh);
	return error;
}

/**
 * do_grow - Make a file look bigger than it is
 * @ip: the inode
 * @size: the size to set the file to
 *
 * Called with an exclusive lock on @ip.
 *
 * Returns: errno
 */

static int do_grow(struct gfs2_inode *ip, u64 size)
{
	struct gfs2_sbd *sdp = GFS2_SB(&ip->i_inode);
	struct gfs2_alloc *al;
	struct buffer_head *dibh;
	unsigned int h;
	int error;

	al = gfs2_alloc_get(ip);

	error = gfs2_quota_lock(ip, NO_QUOTA_CHANGE, NO_QUOTA_CHANGE);
	if (error)
		goto out;

	error = gfs2_quota_check(ip, ip->i_inode.i_uid, ip->i_inode.i_gid);
	if (error)
		goto out_gunlock_q;

	al->al_requested = sdp->sd_max_height + RES_DATA;

	error = gfs2_inplace_reserve(ip);
	if (error)
		goto out_gunlock_q;

	error = gfs2_trans_begin(sdp,
			sdp->sd_max_height + al->al_rgd->rd_length +
			RES_JDATA + RES_DINODE + RES_STATFS + RES_QUOTA, 0);
	if (error)
		goto out_ipres;

	if (size > sdp->sd_sb.sb_bsize - sizeof(struct gfs2_dinode)) {
		if (gfs2_is_stuffed(ip)) {
			error = gfs2_unstuff_dinode(ip, NULL);
			if (error)
				goto out_end_trans;
		}

		h = calc_tree_height(ip, size);
		if (ip->i_di.di_height < h) {
			down_write(&ip->i_rw_mutex);
			error = build_height(&ip->i_inode, h);
			up_write(&ip->i_rw_mutex);
			if (error)
				goto out_end_trans;
		}
	}

	ip->i_di.di_size = size;
	ip->i_inode.i_mtime = ip->i_inode.i_ctime = CURRENT_TIME;

	error = gfs2_meta_inode_buffer(ip, &dibh);
	if (error)
		goto out_end_trans;

	gfs2_trans_add_bh(ip->i_gl, dibh, 1);
	gfs2_dinode_out(ip, dibh->b_data);
	brelse(dibh);

out_end_trans:
	gfs2_trans_end(sdp);
out_ipres:
	gfs2_inplace_release(ip);
out_gunlock_q:
	gfs2_quota_unlock(ip);
out:
	gfs2_alloc_put(ip);
	return error;
}


/**
 * gfs2_block_truncate_page - Deal with zeroing out data for truncate
 *
 * This is partly borrowed from ext3.
 */
static int gfs2_block_truncate_page(struct address_space *mapping)
{
	struct inode *inode = mapping->host;
	struct gfs2_inode *ip = GFS2_I(inode);
	loff_t from = inode->i_size;
	unsigned long index = from >> PAGE_CACHE_SHIFT;
	unsigned offset = from & (PAGE_CACHE_SIZE-1);
	unsigned blocksize, iblock, length, pos;
	struct buffer_head *bh;
	struct page *page;
	int err;

	page = grab_cache_page(mapping, index);
	if (!page)
		return 0;

	blocksize = inode->i_sb->s_blocksize;
	length = blocksize - (offset & (blocksize - 1));
	iblock = index << (PAGE_CACHE_SHIFT - inode->i_sb->s_blocksize_bits);

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
		ll_rw_block(READ, 1, &bh);
		wait_on_buffer(bh);
		/* Uhhuh. Read error. Complain and punt. */
		if (!buffer_uptodate(bh))
			goto unlock;
		err = 0;
	}

	if (!gfs2_is_writeback(ip))
		gfs2_trans_add_bh(ip->i_gl, bh, 0);

	zero_user_page(page, offset, length, KM_USER0);

unlock:
	unlock_page(page);
	page_cache_release(page);
	return err;
}

static int trunc_start(struct gfs2_inode *ip, u64 size)
{
	struct gfs2_sbd *sdp = GFS2_SB(&ip->i_inode);
	struct buffer_head *dibh;
	int journaled = gfs2_is_jdata(ip);
	int error;

	error = gfs2_trans_begin(sdp,
				 RES_DINODE + (journaled ? RES_JDATA : 0), 0);
	if (error)
		return error;

	error = gfs2_meta_inode_buffer(ip, &dibh);
	if (error)
		goto out;

	if (gfs2_is_stuffed(ip)) {
		ip->i_di.di_size = size;
		ip->i_inode.i_mtime = ip->i_inode.i_ctime = CURRENT_TIME;
		gfs2_trans_add_bh(ip->i_gl, dibh, 1);
		gfs2_dinode_out(ip, dibh->b_data);
		gfs2_buffer_clear_tail(dibh, sizeof(struct gfs2_dinode) + size);
		error = 1;

	} else {
		if (size & (u64)(sdp->sd_sb.sb_bsize - 1))
			error = gfs2_block_truncate_page(ip->i_inode.i_mapping);

		if (!error) {
			ip->i_di.di_size = size;
			ip->i_inode.i_mtime = ip->i_inode.i_ctime = CURRENT_TIME;
			ip->i_di.di_flags |= GFS2_DIF_TRUNC_IN_PROG;
			gfs2_trans_add_bh(ip->i_gl, dibh, 1);
			gfs2_dinode_out(ip, dibh->b_data);
		}
	}

	brelse(dibh);

out:
	gfs2_trans_end(sdp);
	return error;
}

static int trunc_dealloc(struct gfs2_inode *ip, u64 size)
{
	unsigned int height = ip->i_di.di_height;
	u64 lblock;
	struct metapath mp;
	int error;

	if (!size)
		lblock = 0;
	else
		lblock = (size - 1) >> GFS2_SB(&ip->i_inode)->sd_sb.sb_bsize_shift;

	find_metapath(ip, lblock, &mp);
	gfs2_alloc_get(ip);

	error = gfs2_quota_hold(ip, NO_QUOTA_CHANGE, NO_QUOTA_CHANGE);
	if (error)
		goto out;

	while (height--) {
		struct strip_mine sm;
		sm.sm_first = !!size;
		sm.sm_height = height;

		error = recursive_scan(ip, NULL, &mp, 0, 0, 1, do_strip, &sm);
		if (error)
			break;
	}

	gfs2_quota_unhold(ip);

out:
	gfs2_alloc_put(ip);
	return error;
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

	if (!ip->i_di.di_size) {
		ip->i_di.di_height = 0;
		ip->i_di.di_goal_meta =
			ip->i_di.di_goal_data =
			ip->i_no_addr;
		gfs2_buffer_clear_tail(dibh, sizeof(struct gfs2_dinode));
	}
	ip->i_inode.i_mtime = ip->i_inode.i_ctime = CURRENT_TIME;
	ip->i_di.di_flags &= ~GFS2_DIF_TRUNC_IN_PROG;

	gfs2_trans_add_bh(ip->i_gl, dibh, 1);
	gfs2_dinode_out(ip, dibh->b_data);
	brelse(dibh);

out:
	up_write(&ip->i_rw_mutex);
	gfs2_trans_end(sdp);
	return error;
}

/**
 * do_shrink - make a file smaller
 * @ip: the inode
 * @size: the size to make the file
 * @truncator: function to truncate the last partial block
 *
 * Called with an exclusive lock on @ip.
 *
 * Returns: errno
 */

static int do_shrink(struct gfs2_inode *ip, u64 size)
{
	int error;

	error = trunc_start(ip, size);
	if (error < 0)
		return error;
	if (error > 0)
		return 0;

	error = trunc_dealloc(ip, size);
	if (!error)
		error = trunc_end(ip);

	return error;
}

static int do_touch(struct gfs2_inode *ip, u64 size)
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
		goto do_touch_out;

	ip->i_inode.i_mtime = ip->i_inode.i_ctime = CURRENT_TIME;
	gfs2_trans_add_bh(ip->i_gl, dibh, 1);
	gfs2_dinode_out(ip, dibh->b_data);
	brelse(dibh);

do_touch_out:
	up_write(&ip->i_rw_mutex);
	gfs2_trans_end(sdp);
	return error;
}

/**
 * gfs2_truncatei - make a file a given size
 * @ip: the inode
 * @size: the size to make the file
 * @truncator: function to truncate the last partial block
 *
 * The file size can grow, shrink, or stay the same size.
 *
 * Returns: errno
 */

int gfs2_truncatei(struct gfs2_inode *ip, u64 size)
{
	int error;

	if (gfs2_assert_warn(GFS2_SB(&ip->i_inode), S_ISREG(ip->i_inode.i_mode)))
		return -EINVAL;

	if (size > ip->i_di.di_size)
		error = do_grow(ip, size);
	else if (size < ip->i_di.di_size)
		error = do_shrink(ip, size);
	else
		/* update time stamps */
		error = do_touch(ip, size);

	return error;
}

int gfs2_truncatei_resume(struct gfs2_inode *ip)
{
	int error;
	error = trunc_dealloc(ip, ip->i_di.di_size);
	if (!error)
		error = trunc_end(ip);
	return error;
}

int gfs2_file_dealloc(struct gfs2_inode *ip)
{
	return trunc_dealloc(ip, 0);
}

/**
 * gfs2_write_calc_reserv - calculate number of blocks needed to write to a file
 * @ip: the file
 * @len: the number of bytes to be written to the file
 * @data_blocks: returns the number of data blocks required
 * @ind_blocks: returns the number of indirect blocks required
 *
 */

void gfs2_write_calc_reserv(struct gfs2_inode *ip, unsigned int len,
			    unsigned int *data_blocks, unsigned int *ind_blocks)
{
	struct gfs2_sbd *sdp = GFS2_SB(&ip->i_inode);
	unsigned int tmp;

	if (gfs2_is_dir(ip)) {
		*data_blocks = DIV_ROUND_UP(len, sdp->sd_jbsize) + 2;
		*ind_blocks = 3 * (sdp->sd_max_jheight - 1);
	} else {
		*data_blocks = (len >> sdp->sd_sb.sb_bsize_shift) + 3;
		*ind_blocks = 3 * (sdp->sd_max_height - 1);
	}

	for (tmp = *data_blocks; tmp > sdp->sd_diptrs;) {
		tmp = DIV_ROUND_UP(tmp, sdp->sd_inptrs);
		*ind_blocks += tmp;
	}
}

/**
 * gfs2_write_alloc_required - figure out if a write will require an allocation
 * @ip: the file being written to
 * @offset: the offset to write to
 * @len: the number of bytes being written
 * @alloc_required: set to 1 if an alloc is required, 0 otherwise
 *
 * Returns: errno
 */

int gfs2_write_alloc_required(struct gfs2_inode *ip, u64 offset,
			      unsigned int len, int *alloc_required)
{
	struct gfs2_sbd *sdp = GFS2_SB(&ip->i_inode);
	u64 lblock, lblock_stop, dblock;
	u32 extlen;
	int new = 0;
	int error = 0;

	*alloc_required = 0;

	if (!len)
		return 0;

	if (gfs2_is_stuffed(ip)) {
		if (offset + len >
		    sdp->sd_sb.sb_bsize - sizeof(struct gfs2_dinode))
			*alloc_required = 1;
		return 0;
	}

	if (gfs2_is_dir(ip)) {
		unsigned int bsize = sdp->sd_jbsize;
		lblock = offset;
		do_div(lblock, bsize);
		lblock_stop = offset + len + bsize - 1;
		do_div(lblock_stop, bsize);
	} else {
		unsigned int shift = sdp->sd_sb.sb_bsize_shift;
		u64 end_of_file = (ip->i_di.di_size + sdp->sd_sb.sb_bsize - 1) >> shift;
		lblock = offset >> shift;
		lblock_stop = (offset + len + sdp->sd_sb.sb_bsize - 1) >> shift;
		if (lblock_stop > end_of_file) {
			*alloc_required = 1;
			return 0;
		}
	}

	for (; lblock < lblock_stop; lblock += extlen) {
		error = gfs2_extent_map(&ip->i_inode, lblock, &new, &dblock, &extlen);
		if (error)
			return error;

		if (!dblock) {
			*alloc_required = 1;
			return 0;
		}
	}

	return 0;
}

