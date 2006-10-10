/* -*- mode: c; c-basic-offset: 8; -*-
 * vim: noexpandtab sw=8 ts=8 sts=0:
 *
 * Copyright (C) 2002, 2004 Oracle.  All rights reserved.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public
 * License along with this program; if not, write to the
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 021110-1307, USA.
 */

#include <linux/fs.h>
#include <linux/slab.h>
#include <linux/highmem.h>
#include <linux/pagemap.h>
#include <asm/byteorder.h>

#define MLOG_MASK_PREFIX ML_FILE_IO
#include <cluster/masklog.h>

#include "ocfs2.h"

#include "alloc.h"
#include "aops.h"
#include "dlmglue.h"
#include "extent_map.h"
#include "file.h"
#include "inode.h"
#include "journal.h"
#include "super.h"
#include "symlink.h"

#include "buffer_head_io.h"

static int ocfs2_symlink_get_block(struct inode *inode, sector_t iblock,
				   struct buffer_head *bh_result, int create)
{
	int err = -EIO;
	int status;
	struct ocfs2_dinode *fe = NULL;
	struct buffer_head *bh = NULL;
	struct buffer_head *buffer_cache_bh = NULL;
	struct ocfs2_super *osb = OCFS2_SB(inode->i_sb);
	void *kaddr;

	mlog_entry("(0x%p, %llu, 0x%p, %d)\n", inode,
		   (unsigned long long)iblock, bh_result, create);

	BUG_ON(ocfs2_inode_is_fast_symlink(inode));

	if ((iblock << inode->i_sb->s_blocksize_bits) > PATH_MAX + 1) {
		mlog(ML_ERROR, "block offset > PATH_MAX: %llu",
		     (unsigned long long)iblock);
		goto bail;
	}

	status = ocfs2_read_block(OCFS2_SB(inode->i_sb),
				  OCFS2_I(inode)->ip_blkno,
				  &bh, OCFS2_BH_CACHED, inode);
	if (status < 0) {
		mlog_errno(status);
		goto bail;
	}
	fe = (struct ocfs2_dinode *) bh->b_data;

	if (!OCFS2_IS_VALID_DINODE(fe)) {
		mlog(ML_ERROR, "Invalid dinode #%llu: signature = %.*s\n",
		     (unsigned long long)fe->i_blkno, 7, fe->i_signature);
		goto bail;
	}

	if ((u64)iblock >= ocfs2_clusters_to_blocks(inode->i_sb,
						    le32_to_cpu(fe->i_clusters))) {
		mlog(ML_ERROR, "block offset is outside the allocated size: "
		     "%llu\n", (unsigned long long)iblock);
		goto bail;
	}

	/* We don't use the page cache to create symlink data, so if
	 * need be, copy it over from the buffer cache. */
	if (!buffer_uptodate(bh_result) && ocfs2_inode_is_new(inode)) {
		u64 blkno = le64_to_cpu(fe->id2.i_list.l_recs[0].e_blkno) +
			    iblock;
		buffer_cache_bh = sb_getblk(osb->sb, blkno);
		if (!buffer_cache_bh) {
			mlog(ML_ERROR, "couldn't getblock for symlink!\n");
			goto bail;
		}

		/* we haven't locked out transactions, so a commit
		 * could've happened. Since we've got a reference on
		 * the bh, even if it commits while we're doing the
		 * copy, the data is still good. */
		if (buffer_jbd(buffer_cache_bh)
		    && ocfs2_inode_is_new(inode)) {
			kaddr = kmap_atomic(bh_result->b_page, KM_USER0);
			if (!kaddr) {
				mlog(ML_ERROR, "couldn't kmap!\n");
				goto bail;
			}
			memcpy(kaddr + (bh_result->b_size * iblock),
			       buffer_cache_bh->b_data,
			       bh_result->b_size);
			kunmap_atomic(kaddr, KM_USER0);
			set_buffer_uptodate(bh_result);
		}
		brelse(buffer_cache_bh);
	}

	map_bh(bh_result, inode->i_sb,
	       le64_to_cpu(fe->id2.i_list.l_recs[0].e_blkno) + iblock);

	err = 0;

bail:
	if (bh)
		brelse(bh);

	mlog_exit(err);
	return err;
}

static int ocfs2_get_block(struct inode *inode, sector_t iblock,
			   struct buffer_head *bh_result, int create)
{
	int err = 0;
	u64 p_blkno, past_eof;

	mlog_entry("(0x%p, %llu, 0x%p, %d)\n", inode,
		   (unsigned long long)iblock, bh_result, create);

	if (OCFS2_I(inode)->ip_flags & OCFS2_INODE_SYSTEM_FILE)
		mlog(ML_NOTICE, "get_block on system inode 0x%p (%lu)\n",
		     inode, inode->i_ino);

	if (S_ISLNK(inode->i_mode)) {
		/* this always does I/O for some reason. */
		err = ocfs2_symlink_get_block(inode, iblock, bh_result, create);
		goto bail;
	}

	/* this can happen if another node truncs after our extend! */
	spin_lock(&OCFS2_I(inode)->ip_lock);
	if (iblock >= ocfs2_clusters_to_blocks(inode->i_sb,
					       OCFS2_I(inode)->ip_clusters))
		err = -EIO;
	spin_unlock(&OCFS2_I(inode)->ip_lock);
	if (err)
		goto bail;

	err = ocfs2_extent_map_get_blocks(inode, iblock, 1, &p_blkno,
					  NULL);
	if (err) {
		mlog(ML_ERROR, "Error %d from get_blocks(0x%p, %llu, 1, "
		     "%llu, NULL)\n", err, inode, (unsigned long long)iblock,
		     (unsigned long long)p_blkno);
		goto bail;
	}

	map_bh(bh_result, inode->i_sb, p_blkno);

	if (bh_result->b_blocknr == 0) {
		err = -EIO;
		mlog(ML_ERROR, "iblock = %llu p_blkno = %llu blkno=(%llu)\n",
		     (unsigned long long)iblock,
		     (unsigned long long)p_blkno,
		     (unsigned long long)OCFS2_I(inode)->ip_blkno);
	}

	past_eof = ocfs2_blocks_for_bytes(inode->i_sb, i_size_read(inode));
	mlog(0, "Inode %lu, past_eof = %llu\n", inode->i_ino,
	     (unsigned long long)past_eof);

	if (create && (iblock >= past_eof))
		set_buffer_new(bh_result);

bail:
	if (err < 0)
		err = -EIO;

	mlog_exit(err);
	return err;
}

static int ocfs2_readpage(struct file *file, struct page *page)
{
	struct inode *inode = page->mapping->host;
	loff_t start = (loff_t)page->index << PAGE_CACHE_SHIFT;
	int ret, unlock = 1;

	mlog_entry("(0x%p, %lu)\n", file, (page ? page->index : 0));

	ret = ocfs2_meta_lock_with_page(inode, NULL, 0, page);
	if (ret != 0) {
		if (ret == AOP_TRUNCATED_PAGE)
			unlock = 0;
		mlog_errno(ret);
		goto out;
	}

	down_read(&OCFS2_I(inode)->ip_alloc_sem);

	/*
	 * i_size might have just been updated as we grabed the meta lock.  We
	 * might now be discovering a truncate that hit on another node.
	 * block_read_full_page->get_block freaks out if it is asked to read
	 * beyond the end of a file, so we check here.  Callers
	 * (generic_file_read, fault->nopage) are clever enough to check i_size
	 * and notice that the page they just read isn't needed.
	 *
	 * XXX sys_readahead() seems to get that wrong?
	 */
	if (start >= i_size_read(inode)) {
		char *addr = kmap(page);
		memset(addr, 0, PAGE_SIZE);
		flush_dcache_page(page);
		kunmap(page);
		SetPageUptodate(page);
		ret = 0;
		goto out_alloc;
	}

	ret = ocfs2_data_lock_with_page(inode, 0, page);
	if (ret != 0) {
		if (ret == AOP_TRUNCATED_PAGE)
			unlock = 0;
		mlog_errno(ret);
		goto out_alloc;
	}

	ret = block_read_full_page(page, ocfs2_get_block);
	unlock = 0;

	ocfs2_data_unlock(inode, 0);
out_alloc:
	up_read(&OCFS2_I(inode)->ip_alloc_sem);
	ocfs2_meta_unlock(inode, 0);
out:
	if (unlock)
		unlock_page(page);
	mlog_exit(ret);
	return ret;
}

/* Note: Because we don't support holes, our allocation has
 * already happened (allocation writes zeros to the file data)
 * so we don't have to worry about ordered writes in
 * ocfs2_writepage.
 *
 * ->writepage is called during the process of invalidating the page cache
 * during blocked lock processing.  It can't block on any cluster locks
 * to during block mapping.  It's relying on the fact that the block
 * mapping can't have disappeared under the dirty pages that it is
 * being asked to write back.
 */
static int ocfs2_writepage(struct page *page, struct writeback_control *wbc)
{
	int ret;

	mlog_entry("(0x%p)\n", page);

	ret = block_write_full_page(page, ocfs2_get_block, wbc);

	mlog_exit(ret);

	return ret;
}

/* This can also be called from ocfs2_write_zero_page() which has done
 * it's own cluster locking. */
int ocfs2_prepare_write_nolock(struct inode *inode, struct page *page,
			       unsigned from, unsigned to)
{
	int ret;

	down_read(&OCFS2_I(inode)->ip_alloc_sem);

	ret = block_prepare_write(page, from, to, ocfs2_get_block);

	up_read(&OCFS2_I(inode)->ip_alloc_sem);

	return ret;
}

/*
 * ocfs2_prepare_write() can be an outer-most ocfs2 call when it is called
 * from loopback.  It must be able to perform its own locking around
 * ocfs2_get_block().
 */
static int ocfs2_prepare_write(struct file *file, struct page *page,
			       unsigned from, unsigned to)
{
	struct inode *inode = page->mapping->host;
	int ret;

	mlog_entry("(0x%p, 0x%p, %u, %u)\n", file, page, from, to);

	ret = ocfs2_meta_lock_with_page(inode, NULL, 0, page);
	if (ret != 0) {
		mlog_errno(ret);
		goto out;
	}

	ret = ocfs2_prepare_write_nolock(inode, page, from, to);

	ocfs2_meta_unlock(inode, 0);
out:
	mlog_exit(ret);
	return ret;
}

/* Taken from ext3. We don't necessarily need the full blown
 * functionality yet, but IMHO it's better to cut and paste the whole
 * thing so we can avoid introducing our own bugs (and easily pick up
 * their fixes when they happen) --Mark */
static int walk_page_buffers(	handle_t *handle,
				struct buffer_head *head,
				unsigned from,
				unsigned to,
				int *partial,
				int (*fn)(	handle_t *handle,
						struct buffer_head *bh))
{
	struct buffer_head *bh;
	unsigned block_start, block_end;
	unsigned blocksize = head->b_size;
	int err, ret = 0;
	struct buffer_head *next;

	for (	bh = head, block_start = 0;
		ret == 0 && (bh != head || !block_start);
	    	block_start = block_end, bh = next)
	{
		next = bh->b_this_page;
		block_end = block_start + blocksize;
		if (block_end <= from || block_start >= to) {
			if (partial && !buffer_uptodate(bh))
				*partial = 1;
			continue;
		}
		err = (*fn)(handle, bh);
		if (!ret)
			ret = err;
	}
	return ret;
}

struct ocfs2_journal_handle *ocfs2_start_walk_page_trans(struct inode *inode,
							 struct page *page,
							 unsigned from,
							 unsigned to)
{
	struct ocfs2_super *osb = OCFS2_SB(inode->i_sb);
	struct ocfs2_journal_handle *handle = NULL;
	int ret = 0;

	handle = ocfs2_start_trans(osb, OCFS2_INODE_UPDATE_CREDITS);
	if (!handle) {
		ret = -ENOMEM;
		mlog_errno(ret);
		goto out;
	}

	if (ocfs2_should_order_data(inode)) {
		ret = walk_page_buffers(handle->k_handle,
					page_buffers(page),
					from, to, NULL,
					ocfs2_journal_dirty_data);
		if (ret < 0) 
			mlog_errno(ret);
	}
out:
	if (ret) {
		if (handle)
			ocfs2_commit_trans(osb, handle);
		handle = ERR_PTR(ret);
	}
	return handle;
}

static int ocfs2_commit_write(struct file *file, struct page *page,
			      unsigned from, unsigned to)
{
	int ret;
	struct buffer_head *di_bh = NULL;
	struct inode *inode = page->mapping->host;
	struct ocfs2_journal_handle *handle = NULL;
	struct ocfs2_dinode *di;

	mlog_entry("(0x%p, 0x%p, %u, %u)\n", file, page, from, to);

	/* NOTE: ocfs2_file_aio_write has ensured that it's safe for
	 * us to continue here without rechecking the I/O against
	 * changed inode values.
	 *
	 * 1) We're currently holding the inode alloc lock, so no
	 *    nodes can change it underneath us.
	 *
	 * 2) We've had to take the metadata lock at least once
	 *    already to check for extending writes, suid removal, etc.
	 *    The meta data update code then ensures that we don't get a
	 *    stale inode allocation image (i_size, i_clusters, etc).
	 */

	ret = ocfs2_meta_lock_with_page(inode, &di_bh, 1, page);
	if (ret != 0) {
		mlog_errno(ret);
		goto out;
	}

	ret = ocfs2_data_lock_with_page(inode, 1, page);
	if (ret != 0) {
		mlog_errno(ret);
		goto out_unlock_meta;
	}

	handle = ocfs2_start_walk_page_trans(inode, page, from, to);
	if (IS_ERR(handle)) {
		ret = PTR_ERR(handle);
		goto out_unlock_data;
	}

	/* Mark our buffer early. We'd rather catch this error up here
	 * as opposed to after a successful commit_write which would
	 * require us to set back inode->i_size. */
	ret = ocfs2_journal_access(handle, inode, di_bh,
				   OCFS2_JOURNAL_ACCESS_WRITE);
	if (ret < 0) {
		mlog_errno(ret);
		goto out_commit;
	}

	/* might update i_size */
	ret = generic_commit_write(file, page, from, to);
	if (ret < 0) {
		mlog_errno(ret);
		goto out_commit;
	}

	di = (struct ocfs2_dinode *)di_bh->b_data;

	/* ocfs2_mark_inode_dirty() is too heavy to use here. */
	inode->i_mtime = inode->i_ctime = CURRENT_TIME;
	di->i_mtime = di->i_ctime = cpu_to_le64(inode->i_mtime.tv_sec);
	di->i_mtime_nsec = di->i_ctime_nsec = cpu_to_le32(inode->i_mtime.tv_nsec);

	inode->i_blocks = ocfs2_align_bytes_to_sectors((u64)(i_size_read(inode)));
	di->i_size = cpu_to_le64((u64)i_size_read(inode));

	ret = ocfs2_journal_dirty(handle, di_bh);
	if (ret < 0) {
		mlog_errno(ret);
		goto out_commit;
	}

out_commit:
	ocfs2_commit_trans(OCFS2_SB(inode->i_sb), handle);
out_unlock_data:
	ocfs2_data_unlock(inode, 1);
out_unlock_meta:
	ocfs2_meta_unlock(inode, 1);
out:
	if (di_bh)
		brelse(di_bh);

	mlog_exit(ret);
	return ret;
}

static sector_t ocfs2_bmap(struct address_space *mapping, sector_t block)
{
	sector_t status;
	u64 p_blkno = 0;
	int err = 0;
	struct inode *inode = mapping->host;

	mlog_entry("(block = %llu)\n", (unsigned long long)block);

	/* We don't need to lock journal system files, since they aren't
	 * accessed concurrently from multiple nodes.
	 */
	if (!INODE_JOURNAL(inode)) {
		err = ocfs2_meta_lock(inode, NULL, 0);
		if (err) {
			if (err != -ENOENT)
				mlog_errno(err);
			goto bail;
		}
		down_read(&OCFS2_I(inode)->ip_alloc_sem);
	}

	err = ocfs2_extent_map_get_blocks(inode, block, 1, &p_blkno,
					  NULL);

	if (!INODE_JOURNAL(inode)) {
		up_read(&OCFS2_I(inode)->ip_alloc_sem);
		ocfs2_meta_unlock(inode, 0);
	}

	if (err) {
		mlog(ML_ERROR, "get_blocks() failed, block = %llu\n",
		     (unsigned long long)block);
		mlog_errno(err);
		goto bail;
	}


bail:
	status = err ? 0 : p_blkno;

	mlog_exit((int)status);

	return status;
}

/*
 * TODO: Make this into a generic get_blocks function.
 *
 * From do_direct_io in direct-io.c:
 *  "So what we do is to permit the ->get_blocks function to populate
 *   bh.b_size with the size of IO which is permitted at this offset and
 *   this i_blkbits."
 *
 * This function is called directly from get_more_blocks in direct-io.c.
 *
 * called like this: dio->get_blocks(dio->inode, fs_startblk,
 * 					fs_count, map_bh, dio->rw == WRITE);
 */
static int ocfs2_direct_IO_get_blocks(struct inode *inode, sector_t iblock,
				     struct buffer_head *bh_result, int create)
{
	int ret;
	u64 vbo_max; /* file offset, max_blocks from iblock */
	u64 p_blkno;
	int contig_blocks;
	unsigned char blocksize_bits = inode->i_sb->s_blocksize_bits;
	unsigned long max_blocks = bh_result->b_size >> inode->i_blkbits;

	/* This function won't even be called if the request isn't all
	 * nicely aligned and of the right size, so there's no need
	 * for us to check any of that. */

	vbo_max = ((u64)iblock + max_blocks) << blocksize_bits;

	spin_lock(&OCFS2_I(inode)->ip_lock);
	if ((iblock + max_blocks) >
	    ocfs2_clusters_to_blocks(inode->i_sb,
				     OCFS2_I(inode)->ip_clusters)) {
		spin_unlock(&OCFS2_I(inode)->ip_lock);
		ret = -EIO;
		goto bail;
	}
	spin_unlock(&OCFS2_I(inode)->ip_lock);

	/* This figures out the size of the next contiguous block, and
	 * our logical offset */
	ret = ocfs2_extent_map_get_blocks(inode, iblock, 1, &p_blkno,
					  &contig_blocks);
	if (ret) {
		mlog(ML_ERROR, "get_blocks() failed iblock=%llu\n",
		     (unsigned long long)iblock);
		ret = -EIO;
		goto bail;
	}

	map_bh(bh_result, inode->i_sb, p_blkno);

	/* make sure we don't map more than max_blocks blocks here as
	   that's all the kernel will handle at this point. */
	if (max_blocks < contig_blocks)
		contig_blocks = max_blocks;
	bh_result->b_size = contig_blocks << blocksize_bits;
bail:
	return ret;
}

/* 
 * ocfs2_dio_end_io is called by the dio core when a dio is finished.  We're
 * particularly interested in the aio/dio case.  Like the core uses
 * i_alloc_sem, we use the rw_lock DLM lock to protect io on one node from
 * truncation on another.
 */
static void ocfs2_dio_end_io(struct kiocb *iocb,
			     loff_t offset,
			     ssize_t bytes,
			     void *private)
{
	struct inode *inode = iocb->ki_filp->f_dentry->d_inode;

	/* this io's submitter should not have unlocked this before we could */
	BUG_ON(!ocfs2_iocb_is_rw_locked(iocb));
	ocfs2_iocb_clear_rw_locked(iocb);
	up_read(&inode->i_alloc_sem);
	ocfs2_rw_unlock(inode, 0);
}

static ssize_t ocfs2_direct_IO(int rw,
			       struct kiocb *iocb,
			       const struct iovec *iov,
			       loff_t offset,
			       unsigned long nr_segs)
{
	struct file *file = iocb->ki_filp;
	struct inode *inode = file->f_dentry->d_inode->i_mapping->host;
	int ret;

	mlog_entry_void();

	/*
	 * We get PR data locks even for O_DIRECT.  This allows
	 * concurrent O_DIRECT I/O but doesn't let O_DIRECT with
	 * extending and buffered zeroing writes race.  If they did
	 * race then the buffered zeroing could be written back after
	 * the O_DIRECT I/O.  It's one thing to tell people not to mix
	 * buffered and O_DIRECT writes, but expecting them to
	 * understand that file extension is also an implicit buffered
	 * write is too much.  By getting the PR we force writeback of
	 * the buffered zeroing before proceeding.
	 */
	ret = ocfs2_data_lock(inode, 0);
	if (ret < 0) {
		mlog_errno(ret);
		goto out;
	}
	ocfs2_data_unlock(inode, 0);

	ret = blockdev_direct_IO_no_locking(rw, iocb, inode,
					    inode->i_sb->s_bdev, iov, offset,
					    nr_segs, 
					    ocfs2_direct_IO_get_blocks,
					    ocfs2_dio_end_io);
out:
	mlog_exit(ret);
	return ret;
}

const struct address_space_operations ocfs2_aops = {
	.readpage	= ocfs2_readpage,
	.writepage	= ocfs2_writepage,
	.prepare_write	= ocfs2_prepare_write,
	.commit_write	= ocfs2_commit_write,
	.bmap		= ocfs2_bmap,
	.sync_page	= block_sync_page,
	.direct_IO	= ocfs2_direct_IO
};
