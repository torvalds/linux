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
#include <asm/semaphore.h>

#include "gfs2.h"
#include "bmap.h"
#include "glock.h"
#include "inode.h"
#include "jdata.h"
#include "log.h"
#include "meta_io.h"
#include "ops_address.h"
#include "page.h"
#include "quota.h"
#include "trans.h"

/**
 * get_block - Fills in a buffer head with details about a block
 * @inode: The inode
 * @lblock: The block number to look up
 * @bh_result: The buffer head to return the result in
 * @create: Non-zero if we may add block to the file
 *
 * Returns: errno
 */

static int get_block(struct inode *inode, sector_t lblock,
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
 * Use Linux VFS block_write_full_page() to write one page,
 *   using GFS2's get_block_noalloc to find which blocks to write.
 */

static int gfs2_writepage(struct page *page, struct writeback_control *wbc)
{
	struct gfs2_inode *ip = get_v2ip(page->mapping->host);
	struct gfs2_sbd *sdp = ip->i_sbd;
	int error;

	atomic_inc(&sdp->sd_ops_address);

	if (gfs2_assert_withdraw(sdp, gfs2_glock_is_held_excl(ip->i_gl))) {
		unlock_page(page);
		return -EIO;
	}
	if (get_transaction) {
		redirty_page_for_writepage(wbc, page);
		unlock_page(page);
		return 0;
	}

	error = block_write_full_page(page, get_block_noalloc, wbc);

	gfs2_meta_cache_flush(ip);

	return error;
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
 * jdata_readpage - readpage that goes through gfs2_jdata_read_mem()
 * @ip:
 * @page: The page to read
 *
 * Returns: errno
 */

static int jdata_readpage(struct gfs2_inode *ip, struct page *page)
{
	void *kaddr;
	int ret;

	kaddr = kmap(page);

	ret = gfs2_jdata_read_mem(ip, kaddr,
				  (uint64_t)page->index << PAGE_CACHE_SHIFT,
				  PAGE_CACHE_SIZE);
	if (ret >= 0) {
		if (ret < PAGE_CACHE_SIZE)
			memset(kaddr + ret, 0, PAGE_CACHE_SIZE - ret);
		SetPageUptodate(page);
		ret = 0;
	}

	kunmap(page);

	unlock_page(page);

	return ret;
}

/**
 * gfs2_readpage - readpage with locking
 * @file: The file to read a page for
 * @page: The page to read
 *
 * Returns: errno
 */

static int gfs2_readpage(struct file *file, struct page *page)
{
	struct gfs2_inode *ip = get_v2ip(page->mapping->host);
	struct gfs2_sbd *sdp = ip->i_sbd;
	int error;

	atomic_inc(&sdp->sd_ops_address);

	if (gfs2_assert_warn(sdp, gfs2_glock_is_locked_by_me(ip->i_gl))) {
		unlock_page(page);
		return -EOPNOTSUPP;
	}

	if (!gfs2_is_jdata(ip)) {
		if (gfs2_is_stuffed(ip)) {
			if (!page->index) {
				error = stuffed_readpage(ip, page);
				unlock_page(page);
			} else
				error = zero_readpage(page);
		} else
			error = block_read_full_page(page, get_block);
	} else
		error = jdata_readpage(ip, page);

	if (unlikely(test_bit(SDF_SHUTDOWN, &sdp->sd_flags)))
		error = -EIO;

	return error;
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
	int error = 0;

	atomic_inc(&sdp->sd_ops_address);

	if (gfs2_assert_warn(sdp, gfs2_glock_is_locked_by_me(ip->i_gl)))
		return -EOPNOTSUPP;

	if (gfs2_is_stuffed(ip)) {
		uint64_t file_size;
		file_size = ((uint64_t)page->index << PAGE_CACHE_SHIFT) + to;

		if (file_size > sdp->sd_sb.sb_bsize -
				sizeof(struct gfs2_dinode)) {
			error = gfs2_unstuff_dinode(ip, gfs2_unstuffer_page,
						    page);
			if (!error)
				error = block_prepare_write(page, from, to,
							    get_block);
		} else if (!PageUptodate(page))
			error = stuffed_readpage(ip, page);
	} else
		error = block_prepare_write(page, from, to, get_block);

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
	int error;

	atomic_inc(&sdp->sd_ops_address);

	if (gfs2_is_stuffed(ip)) {
		struct buffer_head *dibh;
		uint64_t file_size;
		void *kaddr;

		file_size = ((uint64_t)page->index << PAGE_CACHE_SHIFT) + to;

		error = gfs2_meta_inode_buffer(ip, &dibh);
		if (error)
			goto fail;

		gfs2_trans_add_bh(ip->i_gl, dibh, 1);

		kaddr = kmap(page);
		memcpy(dibh->b_data + sizeof(struct gfs2_dinode) + from,
		       (char *)kaddr + from,
		       to - from);
		kunmap(page);

		brelse(dibh);

		SetPageUptodate(page);

		if (inode->i_size < file_size)
			i_size_write(inode, file_size);
	} else {
		if (sdp->sd_args.ar_data == GFS2_DATA_ORDERED)
			gfs2_page_add_databufs(sdp, page, from, to);
		error = generic_commit_write(file, page, from, to);
		if (error)
			goto fail;
	}

	return 0;

 fail:
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
		dblock = generic_block_bmap(mapping, lblock, get_block);

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

	if (gfs2_assert_warn(sdp, gfs2_glock_is_locked_by_me(ip->i_gl)) ||
	    gfs2_assert_warn(sdp, !gfs2_is_stuffed(ip)))
		return -EINVAL;

	if (rw == WRITE && !get_transaction)
		gb = get_blocks_noalloc;

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

