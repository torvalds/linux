/*
 * Copyright (C) Sistina Software, Inc.  1997-2003 All rights reserved.
 * Copyright (C) 2004-2005 Red Hat, Inc.  All rights reserved.
 *
 * This copyrighted material is made available to anyone wishing to use,
 * modify, copy, or redistribute it subject to the terms and conditions
 * of the GNU General Public License v.2.
 */

#include <linux/sched.h>
#include <linux/slab.h>
#include <linux/spinlock.h>
#include <linux/completion.h>
#include <linux/buffer_head.h>
#include <linux/pagemap.h>
#include <linux/mpage.h>
#include <asm/semaphore.h>

#include "gfs2.h"
#include "bmap.h"
#include "glock.h"
#include "inode.h"
#include "log.h"
#include "meta_io.h"
#include "ops_address.h"
#include "page.h"
#include "quota.h"
#include "trans.h"
#include "rgrp.h"

/**
 * gfs2_get_block - Fills in a buffer head with details about a block
 * @inode: The inode
 * @lblock: The block number to look up
 * @bh_result: The buffer head to return the result in
 * @create: Non-zero if we may add block to the file
 *
 * Returns: errno
 */

int gfs2_get_block(struct inode *inode, sector_t lblock,
	           struct buffer_head *bh_result, int create)
{
	struct gfs2_inode *ip = get_v2ip(inode);
	int new = create;
	uint64_t dblock;
	int error;

	error = gfs2_block_map(ip, lblock, &new, &dblock, NULL);
	if (error)
		return error;

	if (!dblock)
		return 0;

	map_bh(bh_result, inode->i_sb, dblock);
	if (new)
		set_buffer_new(bh_result);

	return 0;
}

/**
 * get_block_noalloc - Fills in a buffer head with details about a block
 * @inode: The inode
 * @lblock: The block number to look up
 * @bh_result: The buffer head to return the result in
 * @create: Non-zero if we may add block to the file
 *
 * Returns: errno
 */

static int get_block_noalloc(struct inode *inode, sector_t lblock,
			     struct buffer_head *bh_result, int create)
{
	struct gfs2_inode *ip = get_v2ip(inode);
	int new = 0;
	uint64_t dblock;
	int error;

	error = gfs2_block_map(ip, lblock, &new, &dblock, NULL);
	if (error)
		return error;

	if (dblock)
		map_bh(bh_result, inode->i_sb, dblock);
	else if (gfs2_assert_withdraw(ip->i_sbd, !create))
		error = -EIO;

	return error;
}

static int get_blocks(struct inode *inode, sector_t lblock,
		      unsigned long max_blocks, struct buffer_head *bh_result,
		      int create)
{
	struct gfs2_inode *ip = get_v2ip(inode);
	int new = create;
	uint64_t dblock;
	uint32_t extlen;
	int error;

	error = gfs2_block_map(ip, lblock, &new, &dblock, &extlen);
	if (error)
		return error;

	if (!dblock)
		return 0;

	map_bh(bh_result, inode->i_sb, dblock);
	if (new)
		set_buffer_new(bh_result);

	if (extlen > max_blocks)
		extlen = max_blocks;
	bh_result->b_size = extlen << inode->i_blkbits;

	return 0;
}

static int get_blocks_noalloc(struct inode *inode, sector_t lblock,
			      unsigned long max_blocks,
			      struct buffer_head *bh_result, int create)
{
	struct gfs2_inode *ip = get_v2ip(inode);
	int new = 0;
	uint64_t dblock;
	uint32_t extlen;
	int error;

	error = gfs2_block_map(ip, lblock, &new, &dblock, &extlen);
	if (error)
		return error;

	if (dblock) {
		map_bh(bh_result, inode->i_sb, dblock);
		if (extlen > max_blocks)
			extlen = max_blocks;
		bh_result->b_size = extlen << inode->i_blkbits;
	} else if (gfs2_assert_withdraw(ip->i_sbd, !create))
		error = -EIO;

	return error;
}

/**
 * gfs2_writepage - Write complete page
 * @page: Page to write
 *
 * Returns: errno
 *
 * Some of this is copied from block_write_full_page() although we still
 * call it to do most of the work.
 */

static int gfs2_writepage(struct page *page, struct writeback_control *wbc)
{
	struct inode *inode = page->mapping->host;
	struct gfs2_inode *ip = get_v2ip(page->mapping->host);
	struct gfs2_sbd *sdp = ip->i_sbd;
	loff_t i_size = i_size_read(inode);
	pgoff_t end_index = i_size >> PAGE_CACHE_SHIFT;
	unsigned offset;
	int error;
	int done_trans = 0;

	atomic_inc(&sdp->sd_ops_address);
	if (gfs2_assert_withdraw(sdp, gfs2_glock_is_held_excl(ip->i_gl))) {
		unlock_page(page);
		return -EIO;
	}
	if (get_transaction)
		goto out_ignore;

	/* Is the page fully outside i_size? (truncate in progress) */
        offset = i_size & (PAGE_CACHE_SIZE-1);
	if (page->index >= end_index+1 || !offset) {
		page->mapping->a_ops->invalidatepage(page, 0);
		unlock_page(page);
		return 0; /* don't care */
	}

	if (sdp->sd_args.ar_data == GFS2_DATA_ORDERED || gfs2_is_jdata(ip)) {
		error = gfs2_trans_begin(sdp, RES_DINODE + 1, 0);
		if (error)
			goto out_ignore;
		gfs2_page_add_databufs(ip, page, 0, sdp->sd_vfs->s_blocksize-1);
		done_trans = 1;
	}

	error = block_write_full_page(page, get_block_noalloc, wbc);
	if (done_trans)
		gfs2_trans_end(sdp);
	gfs2_meta_cache_flush(ip);
	return error;

out_ignore:
	redirty_page_for_writepage(wbc, page);
	unlock_page(page);
	return 0;
}

/**
 * stuffed_readpage - Fill in a Linux page with stuffed file data
 * @ip: the inode
 * @page: the page
 *
 * Returns: errno
 */

static int stuffed_readpage(struct gfs2_inode *ip, struct page *page)
{
	struct buffer_head *dibh;
	void *kaddr;
	int error;

	error = gfs2_meta_inode_buffer(ip, &dibh);
	if (error)
		return error;

	kaddr = kmap(page);
	memcpy((char *)kaddr,
	       dibh->b_data + sizeof(struct gfs2_dinode),
	       ip->i_di.di_size);
	memset((char *)kaddr + ip->i_di.di_size,
	       0,
	       PAGE_CACHE_SIZE - ip->i_di.di_size);
	kunmap(page);

	brelse(dibh);

	SetPageUptodate(page);

	return 0;
}

static int zero_readpage(struct page *page)
{
	void *kaddr;

	kaddr = kmap(page);
	memset(kaddr, 0, PAGE_CACHE_SIZE);
	kunmap(page);

	SetPageUptodate(page);
	unlock_page(page);

	return 0;
}

/**
 * gfs2_readpage - readpage with locking
 * @file: The file to read a page for. N.B. This may be NULL if we are
 * reading an internal file.
 * @page: The page to read
 *
 * Returns: errno
 */

static int gfs2_readpage(struct file *file, struct page *page)
{
	struct gfs2_inode *ip = get_v2ip(page->mapping->host);
	struct gfs2_sbd *sdp = ip->i_sbd;
	struct gfs2_holder gh;
	int error;

	atomic_inc(&sdp->sd_ops_address);

	gfs2_holder_init(ip->i_gl, LM_ST_SHARED, GL_ATIME, &gh);
	error = gfs2_glock_nq_m_atime(1, &gh);
	if (error)
		goto out_unlock;

	if (gfs2_is_stuffed(ip)) {
		if (!page->index) {
			error = stuffed_readpage(ip, page);
			unlock_page(page);
		} else
			error = zero_readpage(page);
	} else
		error = mpage_readpage(page, gfs2_get_block);

	if (unlikely(test_bit(SDF_SHUTDOWN, &sdp->sd_flags)))
		error = -EIO;

	gfs2_glock_dq_m(1, &gh);
	gfs2_holder_uninit(&gh);
out:
	return error;
out_unlock:
	unlock_page(page);
	goto out;
}

/**
 * gfs2_prepare_write - Prepare to write a page to a file
 * @file: The file to write to
 * @page: The page which is to be prepared for writing
 * @from: From (byte range within page)
 * @to: To (byte range within page)
 *
 * Returns: errno
 */

static int gfs2_prepare_write(struct file *file, struct page *page,
			      unsigned from, unsigned to)
{
	struct gfs2_inode *ip = get_v2ip(page->mapping->host);
	struct gfs2_sbd *sdp = ip->i_sbd;
	unsigned int data_blocks, ind_blocks, rblocks;
	int alloc_required;
	int error = 0;
	loff_t pos = ((loff_t)page->index << PAGE_CACHE_SHIFT) + from;
	loff_t end = ((loff_t)page->index << PAGE_CACHE_SHIFT) + to;
	struct gfs2_alloc *al;

	atomic_inc(&sdp->sd_ops_address);

	gfs2_holder_init(ip->i_gl, LM_ST_EXCLUSIVE, GL_ATIME, &ip->i_gh);
	error = gfs2_glock_nq_m_atime(1, &ip->i_gh);
	if (error)
		goto out_uninit;

	gfs2_write_calc_reserv(ip, to - from, &data_blocks, &ind_blocks);

	error = gfs2_write_alloc_required(ip, pos, from - to, &alloc_required);
	if (error)
		goto out_unlock;


	if (alloc_required) {
		al = gfs2_alloc_get(ip);

		error = gfs2_quota_lock(ip, NO_QUOTA_CHANGE, NO_QUOTA_CHANGE);
		if (error)
			goto out_alloc_put;

		error = gfs2_quota_check(ip, ip->i_di.di_uid, ip->i_di.di_gid);
		if (error)
			goto out_qunlock;

		al->al_requested = data_blocks + ind_blocks;
		error = gfs2_inplace_reserve(ip);
		if (error)
			goto out_qunlock;
	}

	rblocks = RES_DINODE + ind_blocks;
	if (gfs2_is_jdata(ip))
		rblocks += data_blocks ? data_blocks : 1;
	if (ind_blocks || data_blocks)
		rblocks += RES_STATFS + RES_QUOTA;

	error = gfs2_trans_begin(sdp, rblocks, 0);
	if (error)
		goto out;

	if (gfs2_is_stuffed(ip)) {
		if (end > sdp->sd_sb.sb_bsize - sizeof(struct gfs2_dinode)) {
			error = gfs2_unstuff_dinode(ip, gfs2_unstuffer_page, page);
			if (error)
				goto out;
		} else if (!PageUptodate(page)) {
			error = stuffed_readpage(ip, page);
			goto out;
		}
	}

	error = block_prepare_write(page, from, to, gfs2_get_block);

out:
	if (error) {
		gfs2_trans_end(sdp);
		if (alloc_required) {
			gfs2_inplace_release(ip);
out_qunlock:
			gfs2_quota_unlock(ip);
out_alloc_put:
			gfs2_alloc_put(ip);
		}
out_unlock:
		gfs2_glock_dq_m(1, &ip->i_gh);
out_uninit:
		gfs2_holder_uninit(&ip->i_gh);
	}

	return error;
}

/**
 * gfs2_commit_write - Commit write to a file
 * @file: The file to write to
 * @page: The page containing the data
 * @from: From (byte range within page)
 * @to: To (byte range within page)
 *
 * Returns: errno
 */

static int gfs2_commit_write(struct file *file, struct page *page,
			     unsigned from, unsigned to)
{
	struct inode *inode = page->mapping->host;
	struct gfs2_inode *ip = get_v2ip(inode);
	struct gfs2_sbd *sdp = ip->i_sbd;
	int error = -EOPNOTSUPP;
	struct buffer_head *dibh;
	struct gfs2_alloc *al = &ip->i_alloc;;

	atomic_inc(&sdp->sd_ops_address);


	if (gfs2_assert_withdraw(sdp, gfs2_glock_is_locked_by_me(ip->i_gl)))
                goto fail_nounlock;

	error = gfs2_meta_inode_buffer(ip, &dibh);
	if (error)
		goto fail_endtrans;

	gfs2_trans_add_bh(ip->i_gl, dibh, 1);

	if (gfs2_is_stuffed(ip)) {
		uint64_t file_size;
		void *kaddr;

		file_size = ((uint64_t)page->index << PAGE_CACHE_SHIFT) + to;

		kaddr = kmap_atomic(page, KM_USER0);
		memcpy(dibh->b_data + sizeof(struct gfs2_dinode) + from,
		       (char *)kaddr + from, to - from);
		kunmap_atomic(page, KM_USER0);

		SetPageUptodate(page);

		if (inode->i_size < file_size)
			i_size_write(inode, file_size);
	} else {
		if (sdp->sd_args.ar_data == GFS2_DATA_ORDERED || gfs2_is_jdata(ip))
			gfs2_page_add_databufs(ip, page, from, to);
		error = generic_commit_write(file, page, from, to);
		if (error)
			goto fail;
	}

	if (ip->i_di.di_size < inode->i_size)
		ip->i_di.di_size = inode->i_size;

	gfs2_dinode_out(&ip->i_di, dibh->b_data);
	brelse(dibh);
	gfs2_trans_end(sdp);
	if (al->al_requested) {
		gfs2_inplace_release(ip);
		gfs2_quota_unlock(ip);
		gfs2_alloc_put(ip);
	}
	gfs2_glock_dq_m(1, &ip->i_gh);
	gfs2_holder_uninit(&ip->i_gh);
	return 0;

fail:
	brelse(dibh);
fail_endtrans:
	gfs2_trans_end(sdp);
	if (al->al_requested) {
		gfs2_inplace_release(ip);
		gfs2_quota_unlock(ip);
		gfs2_alloc_put(ip);
	}
	gfs2_glock_dq_m(1, &ip->i_gh);
	gfs2_holder_uninit(&ip->i_gh);
fail_nounlock:
	ClearPageUptodate(page);
	return error;
}

/**
 * gfs2_bmap - Block map function
 * @mapping: Address space info
 * @lblock: The block to map
 *
 * Returns: The disk address for the block or 0 on hole or error
 */

static sector_t gfs2_bmap(struct address_space *mapping, sector_t lblock)
{
	struct gfs2_inode *ip = get_v2ip(mapping->host);
	struct gfs2_holder i_gh;
	sector_t dblock = 0;
	int error;

	atomic_inc(&ip->i_sbd->sd_ops_address);

	error = gfs2_glock_nq_init(ip->i_gl, LM_ST_SHARED, LM_FLAG_ANY, &i_gh);
	if (error)
		return 0;

	if (!gfs2_is_stuffed(ip))
		dblock = generic_block_bmap(mapping, lblock, gfs2_get_block);

	gfs2_glock_dq_uninit(&i_gh);

	return dblock;
}

static void discard_buffer(struct gfs2_sbd *sdp, struct buffer_head *bh)
{
	struct gfs2_bufdata *bd;

	gfs2_log_lock(sdp);
	bd = get_v2bd(bh);
	if (bd) {
		bd->bd_bh = NULL;
		set_v2bd(bh, NULL);
		gfs2_log_unlock(sdp);
		brelse(bh);
	} else
		gfs2_log_unlock(sdp);

	lock_buffer(bh);
	clear_buffer_dirty(bh);
	bh->b_bdev = NULL;
	clear_buffer_mapped(bh);
	clear_buffer_req(bh);
	clear_buffer_new(bh);
	clear_buffer_delay(bh);
	unlock_buffer(bh);
}

static int gfs2_invalidatepage(struct page *page, unsigned long offset)
{
	struct gfs2_sbd *sdp = get_v2sdp(page->mapping->host->i_sb);
	struct buffer_head *head, *bh, *next;
	unsigned int curr_off = 0;
	int ret = 1;

	BUG_ON(!PageLocked(page));
	if (!page_has_buffers(page))
		return 1;

	bh = head = page_buffers(page);
	do {
		unsigned int next_off = curr_off + bh->b_size;
		next = bh->b_this_page;

		if (offset <= curr_off)
			discard_buffer(sdp, bh);

		curr_off = next_off;
		bh = next;
	} while (bh != head);

	if (!offset)
		ret = try_to_release_page(page, 0);

	return ret;
}

static ssize_t gfs2_direct_IO(int rw, struct kiocb *iocb, const struct iovec *iov,
			  loff_t offset, unsigned long nr_segs)
{
	struct file *file = iocb->ki_filp;
	struct inode *inode = file->f_mapping->host;
	struct gfs2_inode *ip = get_v2ip(inode);
	struct gfs2_sbd *sdp = ip->i_sbd;
	get_blocks_t *gb = get_blocks;

	atomic_inc(&sdp->sd_ops_address);

	if (gfs2_is_jdata(ip))
		return -EINVAL;

	if (rw == WRITE) {
		return -EOPNOTSUPP; /* for now */
	} else {
		if (gfs2_assert_warn(sdp, gfs2_glock_is_locked_by_me(ip->i_gl)) ||
		    gfs2_assert_warn(sdp, !gfs2_is_stuffed(ip)))
			return -EINVAL;
	}

	return blockdev_direct_IO(rw, iocb, inode, inode->i_sb->s_bdev, iov,
				  offset, nr_segs, gb, NULL);
}

struct address_space_operations gfs2_file_aops = {
	.writepage = gfs2_writepage,
	.readpage = gfs2_readpage,
	.sync_page = block_sync_page,
	.prepare_write = gfs2_prepare_write,
	.commit_write = gfs2_commit_write,
	.bmap = gfs2_bmap,
	.invalidatepage = gfs2_invalidatepage,
	.direct_IO = gfs2_direct_IO,
};

