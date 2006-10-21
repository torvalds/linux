/*
 * Copyright (C) Sistina Software, Inc.  1997-2003 All rights reserved.
 * Copyright (C) 2004-2006 Red Hat, Inc.  All rights reserved.
 *
 * This copyrighted material is made available to anyone wishing to use,
 * modify, copy, or redistribute it subject to the terms and conditions
 * of the GNU General Public License version 2.
 */

#include <linux/sched.h>
#include <linux/slab.h>
#include <linux/spinlock.h>
#include <linux/completion.h>
#include <linux/buffer_head.h>
#include <linux/mm.h>
#include <linux/pagemap.h>
#include <linux/writeback.h>
#include <linux/swap.h>
#include <linux/delay.h>
#include <linux/bio.h>
#include <linux/gfs2_ondisk.h>
#include <linux/lm_interface.h>

#include "gfs2.h"
#include "incore.h"
#include "glock.h"
#include "glops.h"
#include "inode.h"
#include "log.h"
#include "lops.h"
#include "meta_io.h"
#include "rgrp.h"
#include "trans.h"
#include "util.h"
#include "ops_address.h"

static int aspace_get_block(struct inode *inode, sector_t lblock,
			    struct buffer_head *bh_result, int create)
{
	gfs2_assert_warn(inode->i_sb->s_fs_info, 0);
	return -EOPNOTSUPP;
}

static int gfs2_aspace_writepage(struct page *page,
				 struct writeback_control *wbc)
{
	return block_write_full_page(page, aspace_get_block, wbc);
}

static const struct address_space_operations aspace_aops = {
	.writepage = gfs2_aspace_writepage,
	.releasepage = gfs2_releasepage,
};

/**
 * gfs2_aspace_get - Create and initialize a struct inode structure
 * @sdp: the filesystem the aspace is in
 *
 * Right now a struct inode is just a struct inode.  Maybe Linux
 * will supply a more lightweight address space construct (that works)
 * in the future.
 *
 * Make sure pages/buffers in this aspace aren't in high memory.
 *
 * Returns: the aspace
 */

struct inode *gfs2_aspace_get(struct gfs2_sbd *sdp)
{
	struct inode *aspace;

	aspace = new_inode(sdp->sd_vfs);
	if (aspace) {
		mapping_set_gfp_mask(aspace->i_mapping, GFP_NOFS);
		aspace->i_mapping->a_ops = &aspace_aops;
		aspace->i_size = ~0ULL;
		aspace->i_private = NULL;
		insert_inode_hash(aspace);
	}
	return aspace;
}

void gfs2_aspace_put(struct inode *aspace)
{
	remove_inode_hash(aspace);
	iput(aspace);
}

/**
 * gfs2_meta_inval - Invalidate all buffers associated with a glock
 * @gl: the glock
 *
 */

void gfs2_meta_inval(struct gfs2_glock *gl)
{
	struct gfs2_sbd *sdp = gl->gl_sbd;
	struct inode *aspace = gl->gl_aspace;
	struct address_space *mapping = gl->gl_aspace->i_mapping;

	gfs2_assert_withdraw(sdp, !atomic_read(&gl->gl_ail_count));

	atomic_inc(&aspace->i_writecount);
	truncate_inode_pages(mapping, 0);
	atomic_dec(&aspace->i_writecount);

	gfs2_assert_withdraw(sdp, !mapping->nrpages);
}

/**
 * gfs2_meta_sync - Sync all buffers associated with a glock
 * @gl: The glock
 *
 */

void gfs2_meta_sync(struct gfs2_glock *gl)
{
	struct address_space *mapping = gl->gl_aspace->i_mapping;
	int error;

	filemap_fdatawrite(mapping);
	error = filemap_fdatawait(mapping);

	if (error)
		gfs2_io_error(gl->gl_sbd);
}

/**
 * getbuf - Get a buffer with a given address space
 * @sdp: the filesystem
 * @aspace: the address space
 * @blkno: the block number (filesystem scope)
 * @create: 1 if the buffer should be created
 *
 * Returns: the buffer
 */

static struct buffer_head *getbuf(struct gfs2_sbd *sdp, struct inode *aspace,
				  u64 blkno, int create)
{
	struct page *page;
	struct buffer_head *bh;
	unsigned int shift;
	unsigned long index;
	unsigned int bufnum;

	shift = PAGE_CACHE_SHIFT - sdp->sd_sb.sb_bsize_shift;
	index = blkno >> shift;             /* convert block to page */
	bufnum = blkno - (index << shift);  /* block buf index within page */

	if (create) {
		for (;;) {
			page = grab_cache_page(aspace->i_mapping, index);
			if (page)
				break;
			yield();
		}
	} else {
		page = find_lock_page(aspace->i_mapping, index);
		if (!page)
			return NULL;
	}

	if (!page_has_buffers(page))
		create_empty_buffers(page, sdp->sd_sb.sb_bsize, 0);

	/* Locate header for our buffer within our page */
	for (bh = page_buffers(page); bufnum--; bh = bh->b_this_page)
		/* Do nothing */;
	get_bh(bh);

	if (!buffer_mapped(bh))
		map_bh(bh, sdp->sd_vfs, blkno);

	unlock_page(page);
	mark_page_accessed(page);
	page_cache_release(page);

	return bh;
}

static void meta_prep_new(struct buffer_head *bh)
{
	struct gfs2_meta_header *mh = (struct gfs2_meta_header *)bh->b_data;

	lock_buffer(bh);
	clear_buffer_dirty(bh);
	set_buffer_uptodate(bh);
	unlock_buffer(bh);

	mh->mh_magic = cpu_to_be32(GFS2_MAGIC);
}

/**
 * gfs2_meta_new - Get a block
 * @gl: The glock associated with this block
 * @blkno: The block number
 *
 * Returns: The buffer
 */

struct buffer_head *gfs2_meta_new(struct gfs2_glock *gl, u64 blkno)
{
	struct buffer_head *bh;
	bh = getbuf(gl->gl_sbd, gl->gl_aspace, blkno, CREATE);
	meta_prep_new(bh);
	return bh;
}

/**
 * gfs2_meta_read - Read a block from disk
 * @gl: The glock covering the block
 * @blkno: The block number
 * @flags: flags
 * @bhp: the place where the buffer is returned (NULL on failure)
 *
 * Returns: errno
 */

int gfs2_meta_read(struct gfs2_glock *gl, u64 blkno, int flags,
		   struct buffer_head **bhp)
{
	*bhp = getbuf(gl->gl_sbd, gl->gl_aspace, blkno, CREATE);
	if (!buffer_uptodate(*bhp))
		ll_rw_block(READ_META, 1, bhp);
	if (flags & DIO_WAIT) {
		int error = gfs2_meta_wait(gl->gl_sbd, *bhp);
		if (error) {
			brelse(*bhp);
			return error;
		}
	}

	return 0;
}

/**
 * gfs2_meta_wait - Reread a block from disk
 * @sdp: the filesystem
 * @bh: The block to wait for
 *
 * Returns: errno
 */

int gfs2_meta_wait(struct gfs2_sbd *sdp, struct buffer_head *bh)
{
	if (unlikely(test_bit(SDF_SHUTDOWN, &sdp->sd_flags)))
		return -EIO;

	wait_on_buffer(bh);

	if (!buffer_uptodate(bh)) {
		struct gfs2_trans *tr = current->journal_info;
		if (tr && tr->tr_touched)
			gfs2_io_error_bh(sdp, bh);
		return -EIO;
	}
	if (unlikely(test_bit(SDF_SHUTDOWN, &sdp->sd_flags)))
		return -EIO;

	return 0;
}

/**
 * gfs2_attach_bufdata - attach a struct gfs2_bufdata structure to a buffer
 * @gl: the glock the buffer belongs to
 * @bh: The buffer to be attached to
 * @meta: Flag to indicate whether its metadata or not
 */

void gfs2_attach_bufdata(struct gfs2_glock *gl, struct buffer_head *bh,
			 int meta)
{
	struct gfs2_bufdata *bd;

	if (meta)
		lock_page(bh->b_page);

	if (bh->b_private) {
		if (meta)
			unlock_page(bh->b_page);
		return;
	}

	bd = kmem_cache_alloc(gfs2_bufdata_cachep, GFP_NOFS | __GFP_NOFAIL),
	memset(bd, 0, sizeof(struct gfs2_bufdata));
	bd->bd_bh = bh;
	bd->bd_gl = gl;

	INIT_LIST_HEAD(&bd->bd_list_tr);
	if (meta)
		lops_init_le(&bd->bd_le, &gfs2_buf_lops);
	else
		lops_init_le(&bd->bd_le, &gfs2_databuf_lops);
	bh->b_private = bd;

	if (meta)
		unlock_page(bh->b_page);
}

/**
 * gfs2_pin - Pin a buffer in memory
 * @sdp: the filesystem the buffer belongs to
 * @bh: The buffer to be pinned
 *
 */

void gfs2_pin(struct gfs2_sbd *sdp, struct buffer_head *bh)
{
	struct gfs2_bufdata *bd = bh->b_private;

	gfs2_assert_withdraw(sdp, test_bit(SDF_JOURNAL_LIVE, &sdp->sd_flags));

	if (test_set_buffer_pinned(bh))
		gfs2_assert_withdraw(sdp, 0);

	wait_on_buffer(bh);

	/* If this buffer is in the AIL and it has already been written
	   to in-place disk block, remove it from the AIL. */

	gfs2_log_lock(sdp);
	if (bd->bd_ail && !buffer_in_io(bh))
		list_move(&bd->bd_ail_st_list, &bd->bd_ail->ai_ail2_list);
	gfs2_log_unlock(sdp);

	clear_buffer_dirty(bh);
	wait_on_buffer(bh);

	if (!buffer_uptodate(bh))
		gfs2_io_error_bh(sdp, bh);

	get_bh(bh);
}

/**
 * gfs2_unpin - Unpin a buffer
 * @sdp: the filesystem the buffer belongs to
 * @bh: The buffer to unpin
 * @ai:
 *
 */

void gfs2_unpin(struct gfs2_sbd *sdp, struct buffer_head *bh,
	        struct gfs2_ail *ai)
{
	struct gfs2_bufdata *bd = bh->b_private;

	gfs2_assert_withdraw(sdp, buffer_uptodate(bh));

	if (!buffer_pinned(bh))
		gfs2_assert_withdraw(sdp, 0);

	mark_buffer_dirty(bh);
	clear_buffer_pinned(bh);

	gfs2_log_lock(sdp);
	if (bd->bd_ail) {
		list_del(&bd->bd_ail_st_list);
		brelse(bh);
	} else {
		struct gfs2_glock *gl = bd->bd_gl;
		list_add(&bd->bd_ail_gl_list, &gl->gl_ail_list);
		atomic_inc(&gl->gl_ail_count);
	}
	bd->bd_ail = ai;
	list_add(&bd->bd_ail_st_list, &ai->ai_ail1_list);
	gfs2_log_unlock(sdp);
}

/**
 * gfs2_meta_wipe - make inode's buffers so they aren't dirty/pinned anymore
 * @ip: the inode who owns the buffers
 * @bstart: the first buffer in the run
 * @blen: the number of buffers in the run
 *
 */

void gfs2_meta_wipe(struct gfs2_inode *ip, u64 bstart, u32 blen)
{
	struct gfs2_sbd *sdp = GFS2_SB(&ip->i_inode);
	struct inode *aspace = ip->i_gl->gl_aspace;
	struct buffer_head *bh;

	while (blen) {
		bh = getbuf(sdp, aspace, bstart, NO_CREATE);
		if (bh) {
			struct gfs2_bufdata *bd = bh->b_private;

			if (test_clear_buffer_pinned(bh)) {
				struct gfs2_trans *tr = current->journal_info;
				gfs2_log_lock(sdp);
				list_del_init(&bd->bd_le.le_list);
				gfs2_assert_warn(sdp, sdp->sd_log_num_buf);
				sdp->sd_log_num_buf--;
				gfs2_log_unlock(sdp);
				tr->tr_num_buf_rm++;
				brelse(bh);
			}
			if (bd) {
				gfs2_log_lock(sdp);
				if (bd->bd_ail) {
					u64 blkno = bh->b_blocknr;
					bd->bd_ail = NULL;
					list_del(&bd->bd_ail_st_list);
					list_del(&bd->bd_ail_gl_list);
					atomic_dec(&bd->bd_gl->gl_ail_count);
					brelse(bh);
					gfs2_log_unlock(sdp);
					gfs2_trans_add_revoke(sdp, blkno);
				} else
					gfs2_log_unlock(sdp);
			}

			lock_buffer(bh);
			clear_buffer_dirty(bh);
			clear_buffer_uptodate(bh);
			unlock_buffer(bh);

			brelse(bh);
		}

		bstart++;
		blen--;
	}
}

/**
 * gfs2_meta_cache_flush - get rid of any references on buffers for this inode
 * @ip: The GFS2 inode
 *
 * This releases buffers that are in the most-recently-used array of
 * blocks used for indirect block addressing for this inode.
 */

void gfs2_meta_cache_flush(struct gfs2_inode *ip)
{
	struct buffer_head **bh_slot;
	unsigned int x;

	spin_lock(&ip->i_spin);

	for (x = 0; x < GFS2_MAX_META_HEIGHT; x++) {
		bh_slot = &ip->i_cache[x];
		if (!*bh_slot)
			break;
		brelse(*bh_slot);
		*bh_slot = NULL;
	}

	spin_unlock(&ip->i_spin);
}

/**
 * gfs2_meta_indirect_buffer - Get a metadata buffer
 * @ip: The GFS2 inode
 * @height: The level of this buf in the metadata (indir addr) tree (if any)
 * @num: The block number (device relative) of the buffer
 * @new: Non-zero if we may create a new buffer
 * @bhp: the buffer is returned here
 *
 * Try to use the gfs2_inode's MRU metadata tree cache.
 *
 * Returns: errno
 */

int gfs2_meta_indirect_buffer(struct gfs2_inode *ip, int height, u64 num,
			      int new, struct buffer_head **bhp)
{
	struct gfs2_sbd *sdp = GFS2_SB(&ip->i_inode);
	struct gfs2_glock *gl = ip->i_gl;
	struct buffer_head *bh = NULL, **bh_slot = ip->i_cache + height;
	int in_cache = 0;

	spin_lock(&ip->i_spin);
	if (*bh_slot && (*bh_slot)->b_blocknr == num) {
		bh = *bh_slot;
		get_bh(bh);
		in_cache = 1;
	}
	spin_unlock(&ip->i_spin);

	if (!bh)
		bh = getbuf(gl->gl_sbd, gl->gl_aspace, num, CREATE);

	if (!bh)
		return -ENOBUFS;

	if (new) {
		if (gfs2_assert_warn(sdp, height))
			goto err;
		meta_prep_new(bh);
		gfs2_trans_add_bh(ip->i_gl, bh, 1);
		gfs2_metatype_set(bh, GFS2_METATYPE_IN, GFS2_FORMAT_IN);
		gfs2_buffer_clear_tail(bh, sizeof(struct gfs2_meta_header));
	} else {
		u32 mtype = height ? GFS2_METATYPE_IN : GFS2_METATYPE_DI;
		if (!buffer_uptodate(bh)) {
			ll_rw_block(READ_META, 1, &bh);
			if (gfs2_meta_wait(sdp, bh))
				goto err;
		}
		if (gfs2_metatype_check(sdp, bh, mtype))
			goto err;
	}

	if (!in_cache) {
		spin_lock(&ip->i_spin);
		if (*bh_slot)
			brelse(*bh_slot);
		*bh_slot = bh;
		get_bh(bh);
		spin_unlock(&ip->i_spin);
	}

	*bhp = bh;
	return 0;
err:
	brelse(bh);
	return -EIO;
}

/**
 * gfs2_meta_ra - start readahead on an extent of a file
 * @gl: the glock the blocks belong to
 * @dblock: the starting disk block
 * @extlen: the number of blocks in the extent
 *
 * returns: the first buffer in the extent
 */

struct buffer_head *gfs2_meta_ra(struct gfs2_glock *gl, u64 dblock, u32 extlen)
{
	struct gfs2_sbd *sdp = gl->gl_sbd;
	struct inode *aspace = gl->gl_aspace;
	struct buffer_head *first_bh, *bh;
	u32 max_ra = gfs2_tune_get(sdp, gt_max_readahead) >>
			  sdp->sd_sb.sb_bsize_shift;

	BUG_ON(!extlen);

	if (max_ra < 1)
		max_ra = 1;
	if (extlen > max_ra)
		extlen = max_ra;

	first_bh = getbuf(sdp, aspace, dblock, CREATE);

	if (buffer_uptodate(first_bh))
		goto out;
	if (!buffer_locked(first_bh))
		ll_rw_block(READ_META, 1, &first_bh);

	dblock++;
	extlen--;

	while (extlen) {
		bh = getbuf(sdp, aspace, dblock, CREATE);

		if (!buffer_uptodate(bh) && !buffer_locked(bh))
			ll_rw_block(READA, 1, &bh);
		brelse(bh);
		dblock++;
		extlen--;
		if (!buffer_locked(first_bh) && buffer_uptodate(first_bh))
			goto out;
	}

	wait_on_buffer(first_bh);
out:
	return first_bh;
}

/**
 * gfs2_meta_syncfs - sync all the buffers in a filesystem
 * @sdp: the filesystem
 *
 */

void gfs2_meta_syncfs(struct gfs2_sbd *sdp)
{
	gfs2_log_flush(sdp, NULL);
	for (;;) {
		gfs2_ail1_start(sdp, DIO_ALL);
		if (gfs2_ail1_empty(sdp, DIO_ALL))
			break;
		msleep(10);
	}
}

