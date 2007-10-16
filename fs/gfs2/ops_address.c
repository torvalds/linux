/*
 * Copyright (C) Sistina Software, Inc.  1997-2003 All rights reserved.
 * Copyright (C) 2004-2007 Red Hat, Inc.  All rights reserved.
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
#include <linux/pagemap.h>
#include <linux/pagevec.h>
#include <linux/mpage.h>
#include <linux/fs.h>
#include <linux/writeback.h>
#include <linux/swap.h>
#include <linux/gfs2_ondisk.h>
#include <linux/lm_interface.h>

#include "gfs2.h"
#include "incore.h"
#include "bmap.h"
#include "glock.h"
#include "inode.h"
#include "log.h"
#include "meta_io.h"
#include "ops_address.h"
#include "quota.h"
#include "trans.h"
#include "rgrp.h"
#include "ops_file.h"
#include "super.h"
#include "util.h"
#include "glops.h"


static void gfs2_page_add_databufs(struct gfs2_inode *ip, struct page *page,
				   unsigned int from, unsigned int to)
{
	struct buffer_head *head = page_buffers(page);
	unsigned int bsize = head->b_size;
	struct buffer_head *bh;
	unsigned int start, end;

	for (bh = head, start = 0; bh != head || !start;
	     bh = bh->b_this_page, start = end) {
		end = start + bsize;
		if (end <= from || start >= to)
			continue;
		if (gfs2_is_jdata(ip))
			set_buffer_uptodate(bh);
		gfs2_trans_add_bh(ip->i_gl, bh, 0);
	}
}

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
	return gfs2_block_map(inode, lblock, create, bh_result);
}

/**
 * gfs2_get_block_noalloc - Fills in a buffer head with details about a block
 * @inode: The inode
 * @lblock: The block number to look up
 * @bh_result: The buffer head to return the result in
 * @create: Non-zero if we may add block to the file
 *
 * Returns: errno
 */

static int gfs2_get_block_noalloc(struct inode *inode, sector_t lblock,
				  struct buffer_head *bh_result, int create)
{
	int error;

	error = gfs2_block_map(inode, lblock, 0, bh_result);
	if (error)
		return error;
	if (!buffer_mapped(bh_result))
		return -EIO;
	return 0;
}

static int gfs2_get_block_direct(struct inode *inode, sector_t lblock,
				 struct buffer_head *bh_result, int create)
{
	return gfs2_block_map(inode, lblock, 0, bh_result);
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
	struct gfs2_inode *ip = GFS2_I(inode);
	struct gfs2_sbd *sdp = GFS2_SB(inode);
	loff_t i_size = i_size_read(inode);
	pgoff_t end_index = i_size >> PAGE_CACHE_SHIFT;
	unsigned offset;
	int error;
	int done_trans = 0;

	if (gfs2_assert_withdraw(sdp, gfs2_glock_is_held_excl(ip->i_gl))) {
		unlock_page(page);
		return -EIO;
	}
	if (current->journal_info)
		goto out_ignore;

	/* Is the page fully outside i_size? (truncate in progress) */
        offset = i_size & (PAGE_CACHE_SIZE-1);
	if (page->index > end_index || (page->index == end_index && !offset)) {
		page->mapping->a_ops->invalidatepage(page, 0);
		unlock_page(page);
		return 0; /* don't care */
	}

	if ((sdp->sd_args.ar_data == GFS2_DATA_ORDERED || gfs2_is_jdata(ip)) &&
	    PageChecked(page)) {
		ClearPageChecked(page);
		error = gfs2_trans_begin(sdp, RES_DINODE + 1, 0);
		if (error)
			goto out_ignore;
		if (!page_has_buffers(page)) {
			create_empty_buffers(page, inode->i_sb->s_blocksize,
					     (1 << BH_Dirty)|(1 << BH_Uptodate));
		}
		gfs2_page_add_databufs(ip, page, 0, sdp->sd_vfs->s_blocksize-1);
		done_trans = 1;
	}
	error = block_write_full_page(page, gfs2_get_block_noalloc, wbc);
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
 * gfs2_writepages - Write a bunch of dirty pages back to disk
 * @mapping: The mapping to write
 * @wbc: Write-back control
 *
 * For journaled files and/or ordered writes this just falls back to the
 * kernel's default writepages path for now. We will probably want to change
 * that eventually (i.e. when we look at allocate on flush).
 *
 * For the data=writeback case though we can already ignore buffer heads
 * and write whole extents at once. This is a big reduction in the
 * number of I/O requests we send and the bmap calls we make in this case.
 */
static int gfs2_writepages(struct address_space *mapping,
			   struct writeback_control *wbc)
{
	struct inode *inode = mapping->host;
	struct gfs2_inode *ip = GFS2_I(inode);
	struct gfs2_sbd *sdp = GFS2_SB(inode);

	if (sdp->sd_args.ar_data == GFS2_DATA_WRITEBACK && !gfs2_is_jdata(ip))
		return mpage_writepages(mapping, wbc, gfs2_get_block_noalloc);

	return generic_writepages(mapping, wbc);
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

	/*
	 * Due to the order of unstuffing files and ->nopage(), we can be
	 * asked for a zero page in the case of a stuffed file being extended,
	 * so we need to supply one here. It doesn't happen often.
	 */
	if (unlikely(page->index)) {
		zero_user_page(page, 0, PAGE_CACHE_SIZE, KM_USER0);
		return 0;
	}

	error = gfs2_meta_inode_buffer(ip, &dibh);
	if (error)
		return error;

	kaddr = kmap_atomic(page, KM_USER0);
	memcpy(kaddr, dibh->b_data + sizeof(struct gfs2_dinode),
	       ip->i_di.di_size);
	memset(kaddr + ip->i_di.di_size, 0, PAGE_CACHE_SIZE - ip->i_di.di_size);
	kunmap_atomic(kaddr, KM_USER0);
	flush_dcache_page(page);
	brelse(dibh);
	SetPageUptodate(page);

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
	struct gfs2_inode *ip = GFS2_I(page->mapping->host);
	struct gfs2_sbd *sdp = GFS2_SB(page->mapping->host);
	struct gfs2_file *gf = NULL;
	struct gfs2_holder gh;
	int error;
	int do_unlock = 0;

	if (likely(file != &gfs2_internal_file_sentinel)) {
		if (file) {
			gf = file->private_data;
			if (test_bit(GFF_EXLOCK, &gf->f_flags))
				/* gfs2_sharewrite_fault has grabbed the ip->i_gl already */
				goto skip_lock;
		}
		gfs2_holder_init(ip->i_gl, LM_ST_SHARED, GL_ATIME|LM_FLAG_TRY_1CB, &gh);
		do_unlock = 1;
		error = gfs2_glock_nq_atime(&gh);
		if (unlikely(error))
			goto out_unlock;
	}

skip_lock:
	if (gfs2_is_stuffed(ip)) {
		error = stuffed_readpage(ip, page);
		unlock_page(page);
	} else
		error = mpage_readpage(page, gfs2_get_block);

	if (unlikely(test_bit(SDF_SHUTDOWN, &sdp->sd_flags)))
		error = -EIO;

	if (do_unlock) {
		gfs2_glock_dq_m(1, &gh);
		gfs2_holder_uninit(&gh);
	}
out:
	return error;
out_unlock:
	unlock_page(page);
	if (error == GLR_TRYFAILED) {
		error = AOP_TRUNCATED_PAGE;
		yield();
	}
	if (do_unlock)
		gfs2_holder_uninit(&gh);
	goto out;
}

/**
 * gfs2_readpages - Read a bunch of pages at once
 *
 * Some notes:
 * 1. This is only for readahead, so we can simply ignore any things
 *    which are slightly inconvenient (such as locking conflicts between
 *    the page lock and the glock) and return having done no I/O. Its
 *    obviously not something we'd want to do on too regular a basis.
 *    Any I/O we ignore at this time will be done via readpage later.
 * 2. We don't handle stuffed files here we let readpage do the honours.
 * 3. mpage_readpages() does most of the heavy lifting in the common case.
 * 4. gfs2_get_block() is relied upon to set BH_Boundary in the right places.
 * 5. We use LM_FLAG_TRY_1CB here, effectively we then have lock-ahead as
 *    well as read-ahead.
 */
static int gfs2_readpages(struct file *file, struct address_space *mapping,
			  struct list_head *pages, unsigned nr_pages)
{
	struct inode *inode = mapping->host;
	struct gfs2_inode *ip = GFS2_I(inode);
	struct gfs2_sbd *sdp = GFS2_SB(inode);
	struct gfs2_holder gh;
	int ret = 0;
	int do_unlock = 0;

	if (likely(file != &gfs2_internal_file_sentinel)) {
		if (file) {
			struct gfs2_file *gf = file->private_data;
			if (test_bit(GFF_EXLOCK, &gf->f_flags))
				goto skip_lock;
		}
		gfs2_holder_init(ip->i_gl, LM_ST_SHARED,
				 LM_FLAG_TRY_1CB|GL_ATIME, &gh);
		do_unlock = 1;
		ret = gfs2_glock_nq_atime(&gh);
		if (ret == GLR_TRYFAILED)
			goto out_noerror;
		if (unlikely(ret))
			goto out_unlock;
	}
skip_lock:
	if (!gfs2_is_stuffed(ip))
		ret = mpage_readpages(mapping, pages, nr_pages, gfs2_get_block);

	if (do_unlock) {
		gfs2_glock_dq_m(1, &gh);
		gfs2_holder_uninit(&gh);
	}
out:
	if (unlikely(test_bit(SDF_SHUTDOWN, &sdp->sd_flags)))
		ret = -EIO;
	return ret;
out_noerror:
	ret = 0;
out_unlock:
	if (do_unlock)
		gfs2_holder_uninit(&gh);
	goto out;
}

/**
 * gfs2_write_begin - Begin to write to a file
 * @file: The file to write to
 * @mapping: The mapping in which to write
 * @pos: The file offset at which to start writing
 * @len: Length of the write
 * @flags: Various flags
 * @pagep: Pointer to return the page
 * @fsdata: Pointer to return fs data (unused by GFS2)
 *
 * Returns: errno
 */

static int gfs2_write_begin(struct file *file, struct address_space *mapping,
			    loff_t pos, unsigned len, unsigned flags,
			    struct page **pagep, void **fsdata)
{
	struct gfs2_inode *ip = GFS2_I(mapping->host);
	struct gfs2_sbd *sdp = GFS2_SB(mapping->host);
	unsigned int data_blocks, ind_blocks, rblocks;
	int alloc_required;
	int error = 0;
	struct gfs2_alloc *al;
	pgoff_t index = pos >> PAGE_CACHE_SHIFT;
	unsigned from = pos & (PAGE_CACHE_SIZE - 1);
	unsigned to = from + len;
	struct page *page;

	gfs2_holder_init(ip->i_gl, LM_ST_EXCLUSIVE, GL_ATIME, &ip->i_gh);
	error = gfs2_glock_nq_atime(&ip->i_gh);
	if (unlikely(error))
		goto out_uninit;

	error = -ENOMEM;
	page = __grab_cache_page(mapping, index);
	*pagep = page;
	if (!page)
		goto out_unlock;

	gfs2_write_calc_reserv(ip, len, &data_blocks, &ind_blocks);

	error = gfs2_write_alloc_required(ip, pos, len, &alloc_required);
	if (error)
		goto out_putpage;


	ip->i_alloc.al_requested = 0;
	if (alloc_required) {
		al = gfs2_alloc_get(ip);

		error = gfs2_quota_lock(ip, NO_QUOTA_CHANGE, NO_QUOTA_CHANGE);
		if (error)
			goto out_alloc_put;

		error = gfs2_quota_check(ip, ip->i_inode.i_uid, ip->i_inode.i_gid);
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

	error = gfs2_trans_begin(sdp, rblocks,
				 PAGE_CACHE_SIZE/sdp->sd_sb.sb_bsize);
	if (error)
		goto out_trans_fail;

	if (gfs2_is_stuffed(ip)) {
		if (pos + len > sdp->sd_sb.sb_bsize - sizeof(struct gfs2_dinode)) {
			error = gfs2_unstuff_dinode(ip, page);
			if (error == 0)
				goto prepare_write;
		} else if (!PageUptodate(page))
			error = stuffed_readpage(ip, page);
		goto out;
	}

prepare_write:
	error = block_prepare_write(page, from, to, gfs2_get_block);

out:
	if (error) {
		gfs2_trans_end(sdp);
out_trans_fail:
		if (alloc_required) {
			gfs2_inplace_release(ip);
out_qunlock:
			gfs2_quota_unlock(ip);
out_alloc_put:
			gfs2_alloc_put(ip);
		}
out_putpage:
		page_cache_release(page);
		if (pos + len > ip->i_inode.i_size)
			vmtruncate(&ip->i_inode, ip->i_inode.i_size);
out_unlock:
		gfs2_glock_dq_m(1, &ip->i_gh);
out_uninit:
		gfs2_holder_uninit(&ip->i_gh);
	}

	return error;
}

/**
 * adjust_fs_space - Adjusts the free space available due to gfs2_grow
 * @inode: the rindex inode
 */
static void adjust_fs_space(struct inode *inode)
{
	struct gfs2_sbd *sdp = inode->i_sb->s_fs_info;
	struct gfs2_statfs_change_host *m_sc = &sdp->sd_statfs_master;
	struct gfs2_statfs_change_host *l_sc = &sdp->sd_statfs_local;
	u64 fs_total, new_free;

	/* Total up the file system space, according to the latest rindex. */
	fs_total = gfs2_ri_total(sdp);

	spin_lock(&sdp->sd_statfs_spin);
	if (fs_total > (m_sc->sc_total + l_sc->sc_total))
		new_free = fs_total - (m_sc->sc_total + l_sc->sc_total);
	else
		new_free = 0;
	spin_unlock(&sdp->sd_statfs_spin);
	fs_warn(sdp, "File system extended by %llu blocks.\n",
		(unsigned long long)new_free);
	gfs2_statfs_change(sdp, new_free, new_free, 0);
}

/**
 * gfs2_stuffed_write_end - Write end for stuffed files
 * @inode: The inode
 * @dibh: The buffer_head containing the on-disk inode
 * @pos: The file position
 * @len: The length of the write
 * @copied: How much was actually copied by the VFS
 * @page: The page
 *
 * This copies the data from the page into the inode block after
 * the inode data structure itself.
 *
 * Returns: errno
 */
static int gfs2_stuffed_write_end(struct inode *inode, struct buffer_head *dibh,
				  loff_t pos, unsigned len, unsigned copied,
				  struct page *page)
{
	struct gfs2_inode *ip = GFS2_I(inode);
	struct gfs2_sbd *sdp = GFS2_SB(inode);
	u64 to = pos + copied;
	void *kaddr;
	unsigned char *buf = dibh->b_data + sizeof(struct gfs2_dinode);
	struct gfs2_dinode *di = (struct gfs2_dinode *)dibh->b_data;

	BUG_ON((pos + len) > (dibh->b_size - sizeof(struct gfs2_dinode)));
	kaddr = kmap_atomic(page, KM_USER0);
	memcpy(buf + pos, kaddr + pos, copied);
	memset(kaddr + pos + copied, 0, len - copied);
	flush_dcache_page(page);
	kunmap_atomic(kaddr, KM_USER0);

	if (!PageUptodate(page))
		SetPageUptodate(page);
	unlock_page(page);
	page_cache_release(page);

	if (inode->i_size < to) {
		i_size_write(inode, to);
		ip->i_di.di_size = inode->i_size;
		di->di_size = cpu_to_be64(inode->i_size);
		mark_inode_dirty(inode);
	}

	if (inode == sdp->sd_rindex)
		adjust_fs_space(inode);

	brelse(dibh);
	gfs2_trans_end(sdp);
	gfs2_glock_dq(&ip->i_gh);
	gfs2_holder_uninit(&ip->i_gh);
	return copied;
}

/**
 * gfs2_write_end
 * @file: The file to write to
 * @mapping: The address space to write to
 * @pos: The file position
 * @len: The length of the data
 * @copied:
 * @page: The page that has been written
 * @fsdata: The fsdata (unused in GFS2)
 *
 * The main write_end function for GFS2. We have a separate one for
 * stuffed files as they are slightly different, otherwise we just
 * put our locking around the VFS provided functions.
 *
 * Returns: errno
 */

static int gfs2_write_end(struct file *file, struct address_space *mapping,
			  loff_t pos, unsigned len, unsigned copied,
			  struct page *page, void *fsdata)
{
	struct inode *inode = page->mapping->host;
	struct gfs2_inode *ip = GFS2_I(inode);
	struct gfs2_sbd *sdp = GFS2_SB(inode);
	struct buffer_head *dibh;
	struct gfs2_alloc *al = &ip->i_alloc;
	struct gfs2_dinode *di;
	unsigned int from = pos & (PAGE_CACHE_SIZE - 1);
	unsigned int to = from + len;
	int ret;

	BUG_ON(gfs2_glock_is_locked_by_me(ip->i_gl) == 0);

	ret = gfs2_meta_inode_buffer(ip, &dibh);
	if (unlikely(ret)) {
		unlock_page(page);
		page_cache_release(page);
		goto failed;
	}

	gfs2_trans_add_bh(ip->i_gl, dibh, 1);

	if (gfs2_is_stuffed(ip))
		return gfs2_stuffed_write_end(inode, dibh, pos, len, copied, page);

	if (sdp->sd_args.ar_data == GFS2_DATA_ORDERED || gfs2_is_jdata(ip))
		gfs2_page_add_databufs(ip, page, from, to);

	ret = generic_write_end(file, mapping, pos, len, copied, page, fsdata);

	if (likely(ret >= 0)) {
		copied = ret;
		if  ((pos + copied) > inode->i_size) {
			di = (struct gfs2_dinode *)dibh->b_data;
			ip->i_di.di_size = inode->i_size;
			di->di_size = cpu_to_be64(inode->i_size);
			mark_inode_dirty(inode);
		}
	}

	if (inode == sdp->sd_rindex)
		adjust_fs_space(inode);

	brelse(dibh);
	gfs2_trans_end(sdp);
failed:
	if (al->al_requested) {
		gfs2_inplace_release(ip);
		gfs2_quota_unlock(ip);
		gfs2_alloc_put(ip);
	}
	gfs2_glock_dq(&ip->i_gh);
	gfs2_holder_uninit(&ip->i_gh);
	return ret;
}

/**
 * gfs2_set_page_dirty - Page dirtying function
 * @page: The page to dirty
 *
 * Returns: 1 if it dirtyed the page, or 0 otherwise
 */
 
static int gfs2_set_page_dirty(struct page *page)
{
	struct gfs2_inode *ip = GFS2_I(page->mapping->host);
	struct gfs2_sbd *sdp = GFS2_SB(page->mapping->host);

	if (sdp->sd_args.ar_data == GFS2_DATA_ORDERED || gfs2_is_jdata(ip))
		SetPageChecked(page);
	return __set_page_dirty_buffers(page);
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
	struct gfs2_inode *ip = GFS2_I(mapping->host);
	struct gfs2_holder i_gh;
	sector_t dblock = 0;
	int error;

	error = gfs2_glock_nq_init(ip->i_gl, LM_ST_SHARED, LM_FLAG_ANY, &i_gh);
	if (error)
		return 0;

	if (!gfs2_is_stuffed(ip))
		dblock = generic_block_bmap(mapping, lblock, gfs2_get_block);

	gfs2_glock_dq_uninit(&i_gh);

	return dblock;
}

static void gfs2_discard(struct gfs2_sbd *sdp, struct buffer_head *bh)
{
	struct gfs2_bufdata *bd;

	lock_buffer(bh);
	gfs2_log_lock(sdp);
	clear_buffer_dirty(bh);
	bd = bh->b_private;
	if (bd) {
		if (!list_empty(&bd->bd_le.le_list) && !buffer_pinned(bh))
			list_del_init(&bd->bd_le.le_list);
		else
			gfs2_remove_from_journal(bh, current->journal_info, 0);
	}
	bh->b_bdev = NULL;
	clear_buffer_mapped(bh);
	clear_buffer_req(bh);
	clear_buffer_new(bh);
	gfs2_log_unlock(sdp);
	unlock_buffer(bh);
}

static void gfs2_invalidatepage(struct page *page, unsigned long offset)
{
	struct gfs2_sbd *sdp = GFS2_SB(page->mapping->host);
	struct buffer_head *bh, *head;
	unsigned long pos = 0;

	BUG_ON(!PageLocked(page));
	if (offset == 0)
		ClearPageChecked(page);
	if (!page_has_buffers(page))
		goto out;

	bh = head = page_buffers(page);
	do {
		if (offset <= pos)
			gfs2_discard(sdp, bh);
		pos += bh->b_size;
		bh = bh->b_this_page;
	} while (bh != head);
out:
	if (offset == 0)
		try_to_release_page(page, 0);
}

/**
 * gfs2_ok_for_dio - check that dio is valid on this file
 * @ip: The inode
 * @rw: READ or WRITE
 * @offset: The offset at which we are reading or writing
 *
 * Returns: 0 (to ignore the i/o request and thus fall back to buffered i/o)
 *          1 (to accept the i/o request)
 */
static int gfs2_ok_for_dio(struct gfs2_inode *ip, int rw, loff_t offset)
{
	/*
	 * Should we return an error here? I can't see that O_DIRECT for
	 * a journaled file makes any sense. For now we'll silently fall
	 * back to buffered I/O, likewise we do the same for stuffed
	 * files since they are (a) small and (b) unaligned.
	 */
	if (gfs2_is_jdata(ip))
		return 0;

	if (gfs2_is_stuffed(ip))
		return 0;

	if (offset > i_size_read(&ip->i_inode))
		return 0;
	return 1;
}



static ssize_t gfs2_direct_IO(int rw, struct kiocb *iocb,
			      const struct iovec *iov, loff_t offset,
			      unsigned long nr_segs)
{
	struct file *file = iocb->ki_filp;
	struct inode *inode = file->f_mapping->host;
	struct gfs2_inode *ip = GFS2_I(inode);
	struct gfs2_holder gh;
	int rv;

	/*
	 * Deferred lock, even if its a write, since we do no allocation
	 * on this path. All we need change is atime, and this lock mode
	 * ensures that other nodes have flushed their buffered read caches
	 * (i.e. their page cache entries for this inode). We do not,
	 * unfortunately have the option of only flushing a range like
	 * the VFS does.
	 */
	gfs2_holder_init(ip->i_gl, LM_ST_DEFERRED, GL_ATIME, &gh);
	rv = gfs2_glock_nq_atime(&gh);
	if (rv)
		return rv;
	rv = gfs2_ok_for_dio(ip, rw, offset);
	if (rv != 1)
		goto out; /* dio not valid, fall back to buffered i/o */

	rv = blockdev_direct_IO_no_locking(rw, iocb, inode, inode->i_sb->s_bdev,
					   iov, offset, nr_segs,
					   gfs2_get_block_direct, NULL);
out:
	gfs2_glock_dq_m(1, &gh);
	gfs2_holder_uninit(&gh);
	return rv;
}

/**
 * gfs2_releasepage - free the metadata associated with a page
 * @page: the page that's being released
 * @gfp_mask: passed from Linux VFS, ignored by us
 *
 * Call try_to_free_buffers() if the buffers in this page can be
 * released.
 *
 * Returns: 0
 */

int gfs2_releasepage(struct page *page, gfp_t gfp_mask)
{
	struct inode *aspace = page->mapping->host;
	struct gfs2_sbd *sdp = aspace->i_sb->s_fs_info;
	struct buffer_head *bh, *head;
	struct gfs2_bufdata *bd;

	if (!page_has_buffers(page))
		return 0;

	gfs2_log_lock(sdp);
	head = bh = page_buffers(page);
	do {
		if (atomic_read(&bh->b_count))
			goto cannot_release;
		bd = bh->b_private;
		if (bd && bd->bd_ail)
			goto cannot_release;
		gfs2_assert_warn(sdp, !buffer_pinned(bh));
		gfs2_assert_warn(sdp, !buffer_dirty(bh));
		bh = bh->b_this_page;
	} while(bh != head);
	gfs2_log_unlock(sdp);

	head = bh = page_buffers(page);
	do {
		gfs2_log_lock(sdp);
		bd = bh->b_private;
		if (bd) {
			gfs2_assert_warn(sdp, bd->bd_bh == bh);
			gfs2_assert_warn(sdp, list_empty(&bd->bd_list_tr));
			if (!list_empty(&bd->bd_le.le_list)) {
				if (!buffer_pinned(bh))
					list_del_init(&bd->bd_le.le_list);
				else
					bd = NULL;
			}
			if (bd)
				bd->bd_bh = NULL;
			bh->b_private = NULL;
		}
		gfs2_log_unlock(sdp);
		if (bd)
			kmem_cache_free(gfs2_bufdata_cachep, bd);

		bh = bh->b_this_page;
	} while (bh != head);

	return try_to_free_buffers(page);
cannot_release:
	gfs2_log_unlock(sdp);
	return 0;
}

const struct address_space_operations gfs2_file_aops = {
	.writepage = gfs2_writepage,
	.writepages = gfs2_writepages,
	.readpage = gfs2_readpage,
	.readpages = gfs2_readpages,
	.sync_page = block_sync_page,
	.write_begin = gfs2_write_begin,
	.write_end = gfs2_write_end,
	.set_page_dirty = gfs2_set_page_dirty,
	.bmap = gfs2_bmap,
	.invalidatepage = gfs2_invalidatepage,
	.releasepage = gfs2_releasepage,
	.direct_IO = gfs2_direct_IO,
};

