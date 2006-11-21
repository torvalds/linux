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
#include <linux/pagemap.h>
#include <linux/pagevec.h>
#include <linux/mpage.h>
#include <linux/fs.h>
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
	if (bh_result->b_blocknr == 0)
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

	if (sdp->sd_args.ar_data == GFS2_DATA_ORDERED || gfs2_is_jdata(ip)) {
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

static int zero_readpage(struct page *page)
{
	void *kaddr;

	kaddr = kmap_atomic(page, KM_USER0);
	memset(kaddr, 0, PAGE_CACHE_SIZE);
	kunmap_atomic(kaddr, KM_USER0);

	SetPageUptodate(page);

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

	/* Only the first page of a stuffed file might contain data */
	if (unlikely(page->index))
		return zero_readpage(page);

	error = gfs2_meta_inode_buffer(ip, &dibh);
	if (error)
		return error;

	kaddr = kmap_atomic(page, KM_USER0);
	memcpy(kaddr, dibh->b_data + sizeof(struct gfs2_dinode),
	       ip->i_di.di_size);
	memset(kaddr + ip->i_di.di_size, 0, PAGE_CACHE_SIZE - ip->i_di.di_size);
	kunmap_atomic(kaddr, KM_USER0);

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
				/* gfs2_sharewrite_nopage has grabbed the ip->i_gl already */
				goto skip_lock;
		}
		gfs2_holder_init(ip->i_gl, LM_ST_SHARED, GL_ATIME|GL_AOP, &gh);
		do_unlock = 1;
		error = gfs2_glock_nq_m_atime(1, &gh);
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
 * 2. We have to handle stuffed files here too.
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
	unsigned page_idx;
	int ret;
	int do_unlock = 0;

	if (likely(file != &gfs2_internal_file_sentinel)) {
		if (file) {
			struct gfs2_file *gf = file->private_data;
			if (test_bit(GFF_EXLOCK, &gf->f_flags))
				goto skip_lock;
		}
		gfs2_holder_init(ip->i_gl, LM_ST_SHARED,
				 LM_FLAG_TRY_1CB|GL_ATIME|GL_AOP, &gh);
		do_unlock = 1;
		ret = gfs2_glock_nq_m_atime(1, &gh);
		if (ret == GLR_TRYFAILED)
			goto out_noerror;
		if (unlikely(ret))
			goto out_unlock;
	}
skip_lock:
	if (gfs2_is_stuffed(ip)) {
		struct pagevec lru_pvec;
		pagevec_init(&lru_pvec, 0);
		for (page_idx = 0; page_idx < nr_pages; page_idx++) {
			struct page *page = list_entry(pages->prev, struct page, lru);
			prefetchw(&page->flags);
			list_del(&page->lru);
			if (!add_to_page_cache(page, mapping,
					       page->index, GFP_KERNEL)) {
				ret = stuffed_readpage(ip, page);
				unlock_page(page);
				if (!pagevec_add(&lru_pvec, page))
					 __pagevec_lru_add(&lru_pvec);
			} else {
				page_cache_release(page);
			}
		}
		pagevec_lru_add(&lru_pvec);
		ret = 0;
	} else {
		/* What we really want to do .... */
		ret = mpage_readpages(mapping, pages, nr_pages, gfs2_get_block);
	}

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
	struct gfs2_inode *ip = GFS2_I(page->mapping->host);
	struct gfs2_sbd *sdp = GFS2_SB(page->mapping->host);
	unsigned int data_blocks, ind_blocks, rblocks;
	int alloc_required;
	int error = 0;
	loff_t pos = ((loff_t)page->index << PAGE_CACHE_SHIFT) + from;
	loff_t end = ((loff_t)page->index << PAGE_CACHE_SHIFT) + to;
	struct gfs2_alloc *al;
	unsigned int write_len = to - from;


	gfs2_holder_init(ip->i_gl, LM_ST_EXCLUSIVE, GL_ATIME|GL_AOP, &ip->i_gh);
	error = gfs2_glock_nq_m_atime(1, &ip->i_gh);
	if (error)
		goto out_uninit;

	gfs2_write_calc_reserv(ip, write_len, &data_blocks, &ind_blocks);

	error = gfs2_write_alloc_required(ip, pos, write_len, &alloc_required);
	if (error)
		goto out_unlock;


	ip->i_alloc.al_requested = 0;
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
	struct gfs2_inode *ip = GFS2_I(inode);
	struct gfs2_sbd *sdp = GFS2_SB(inode);
	int error = -EOPNOTSUPP;
	struct buffer_head *dibh;
	struct gfs2_alloc *al = &ip->i_alloc;
	struct gfs2_dinode *di;

	if (gfs2_assert_withdraw(sdp, gfs2_glock_is_locked_by_me(ip->i_gl)))
                goto fail_nounlock;

	error = gfs2_meta_inode_buffer(ip, &dibh);
	if (error)
		goto fail_endtrans;

	gfs2_trans_add_bh(ip->i_gl, dibh, 1);
	di = (struct gfs2_dinode *)dibh->b_data;

	if (gfs2_is_stuffed(ip)) {
		u64 file_size;
		void *kaddr;

		file_size = ((u64)page->index << PAGE_CACHE_SHIFT) + to;

		kaddr = kmap_atomic(page, KM_USER0);
		memcpy(dibh->b_data + sizeof(struct gfs2_dinode) + from,
		       kaddr + from, to - from);
		kunmap_atomic(kaddr, KM_USER0);

		SetPageUptodate(page);

		if (inode->i_size < file_size)
			i_size_write(inode, file_size);
	} else {
		if (sdp->sd_args.ar_data == GFS2_DATA_ORDERED ||
		    gfs2_is_jdata(ip))
			gfs2_page_add_databufs(ip, page, from, to);
		error = generic_commit_write(file, page, from, to);
		if (error)
			goto fail;
	}

	if (ip->i_di.di_size < inode->i_size) {
		ip->i_di.di_size = inode->i_size;
		di->di_size = cpu_to_be64(inode->i_size);
	}

	di->di_mode = cpu_to_be32(inode->i_mode);
	di->di_atime = cpu_to_be64(inode->i_atime.tv_sec);
	di->di_mtime = cpu_to_be64(inode->i_mtime.tv_sec);
	di->di_ctime = cpu_to_be64(inode->i_ctime.tv_sec);

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

static void discard_buffer(struct gfs2_sbd *sdp, struct buffer_head *bh)
{
	struct gfs2_bufdata *bd;

	gfs2_log_lock(sdp);
	bd = bh->b_private;
	if (bd) {
		bd->bd_bh = NULL;
		bh->b_private = NULL;
	}
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

static void gfs2_invalidatepage(struct page *page, unsigned long offset)
{
	struct gfs2_sbd *sdp = GFS2_SB(page->mapping->host);
	struct buffer_head *head, *bh, *next;
	unsigned int curr_off = 0;

	BUG_ON(!PageLocked(page));
	if (!page_has_buffers(page))
		return;

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
		try_to_release_page(page, 0);

	return;
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

	if (rw == READ)
		mutex_lock(&inode->i_mutex);
	/*
	 * Shared lock, even if its a write, since we do no allocation
	 * on this path. All we need change is atime.
	 */
	gfs2_holder_init(ip->i_gl, LM_ST_SHARED, GL_ATIME, &gh);
	rv = gfs2_glock_nq_m_atime(1, &gh);
	if (rv)
		goto out;

	if (offset > i_size_read(inode))
		goto out;

	/*
	 * Should we return an error here? I can't see that O_DIRECT for
	 * a journaled file makes any sense. For now we'll silently fall
	 * back to buffered I/O, likewise we do the same for stuffed
	 * files since they are (a) small and (b) unaligned.
	 */
	if (gfs2_is_jdata(ip))
		goto out;

	if (gfs2_is_stuffed(ip))
		goto out;

	rv = blockdev_direct_IO_own_locking(rw, iocb, inode,
					    inode->i_sb->s_bdev,
					    iov, offset, nr_segs,
					    gfs2_get_block_direct, NULL);
out:
	gfs2_glock_dq_m(1, &gh);
	gfs2_holder_uninit(&gh);
	if (rw == READ)
		mutex_unlock(&inode->i_mutex);

	return rv;
}

/**
 * stuck_releasepage - We're stuck in gfs2_releasepage().  Print stuff out.
 * @bh: the buffer we're stuck on
 *
 */

static void stuck_releasepage(struct buffer_head *bh)
{
	struct inode *inode = bh->b_page->mapping->host;
	struct gfs2_sbd *sdp = inode->i_sb->s_fs_info;
	struct gfs2_bufdata *bd = bh->b_private;
	struct gfs2_glock *gl;
static unsigned limit = 0;

	if (limit > 3)
		return;
	limit++;

	fs_warn(sdp, "stuck in gfs2_releasepage() %p\n", inode);
	fs_warn(sdp, "blkno = %llu, bh->b_count = %d\n",
		(unsigned long long)bh->b_blocknr, atomic_read(&bh->b_count));
	fs_warn(sdp, "pinned = %u\n", buffer_pinned(bh));
	fs_warn(sdp, "bh->b_private = %s\n", (bd) ? "!NULL" : "NULL");

	if (!bd)
		return;

	gl = bd->bd_gl;

	fs_warn(sdp, "gl = (%u, %llu)\n",
		gl->gl_name.ln_type, (unsigned long long)gl->gl_name.ln_number);

	fs_warn(sdp, "bd_list_tr = %s, bd_le.le_list = %s\n",
		(list_empty(&bd->bd_list_tr)) ? "no" : "yes",
		(list_empty(&bd->bd_le.le_list)) ? "no" : "yes");

	if (gl->gl_ops == &gfs2_inode_glops) {
		struct gfs2_inode *ip = gl->gl_object;
		unsigned int x;

		if (!ip)
			return;

		fs_warn(sdp, "ip = %llu %llu\n",
			(unsigned long long)ip->i_num.no_formal_ino,
			(unsigned long long)ip->i_num.no_addr);

		for (x = 0; x < GFS2_MAX_META_HEIGHT; x++)
			fs_warn(sdp, "ip->i_cache[%u] = %s\n",
				x, (ip->i_cache[x]) ? "!NULL" : "NULL");
	}
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
	unsigned long t = jiffies + gfs2_tune_get(sdp, gt_stall_secs) * HZ;

	if (!page_has_buffers(page))
		goto out;

	head = bh = page_buffers(page);
	do {
		while (atomic_read(&bh->b_count)) {
			if (!atomic_read(&aspace->i_writecount))
				return 0;

			if (time_after_eq(jiffies, t)) {
				stuck_releasepage(bh);
				/* should we withdraw here? */
				return 0;
			}

			yield();
		}

		gfs2_assert_warn(sdp, !buffer_pinned(bh));
		gfs2_assert_warn(sdp, !buffer_dirty(bh));

		gfs2_log_lock(sdp);
		bd = bh->b_private;
		if (bd) {
			gfs2_assert_warn(sdp, bd->bd_bh == bh);
			gfs2_assert_warn(sdp, list_empty(&bd->bd_list_tr));
			gfs2_assert_warn(sdp, !bd->bd_ail);
			bd->bd_bh = NULL;
			if (!list_empty(&bd->bd_le.le_list))
				bd = NULL;
			bh->b_private = NULL;
		}
		gfs2_log_unlock(sdp);
		if (bd)
			kmem_cache_free(gfs2_bufdata_cachep, bd);

		bh = bh->b_this_page;
	} while (bh != head);

out:
	return try_to_free_buffers(page);
}

const struct address_space_operations gfs2_file_aops = {
	.writepage = gfs2_writepage,
	.readpage = gfs2_readpage,
	.readpages = gfs2_readpages,
	.sync_page = block_sync_page,
	.prepare_write = gfs2_prepare_write,
	.commit_write = gfs2_commit_write,
	.bmap = gfs2_bmap,
	.invalidatepage = gfs2_invalidatepage,
	.releasepage = gfs2_releasepage,
	.direct_IO = gfs2_direct_IO,
};

