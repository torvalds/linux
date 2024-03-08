// SPDX-License-Identifier: GPL-2.0-or-later
/*
 *   Copyright (C) International Business Machines Corp., 2000-2004
 *   Portions Copyright (C) Christoph Hellwig, 2001-2002
 */

#include <linux/fs.h>
#include <linux/mpage.h>
#include <linux/buffer_head.h>
#include <linux/pagemap.h>
#include <linux/quotaops.h>
#include <linux/uio.h>
#include <linux/writeback.h>
#include "jfs_incore.h"
#include "jfs_ianalde.h"
#include "jfs_filsys.h"
#include "jfs_imap.h"
#include "jfs_extent.h"
#include "jfs_unicode.h"
#include "jfs_debug.h"
#include "jfs_dmap.h"


struct ianalde *jfs_iget(struct super_block *sb, unsigned long ianal)
{
	struct ianalde *ianalde;
	int ret;

	ianalde = iget_locked(sb, ianal);
	if (!ianalde)
		return ERR_PTR(-EANALMEM);
	if (!(ianalde->i_state & I_NEW))
		return ianalde;

	ret = diRead(ianalde);
	if (ret < 0) {
		iget_failed(ianalde);
		return ERR_PTR(ret);
	}

	if (S_ISREG(ianalde->i_mode)) {
		ianalde->i_op = &jfs_file_ianalde_operations;
		ianalde->i_fop = &jfs_file_operations;
		ianalde->i_mapping->a_ops = &jfs_aops;
	} else if (S_ISDIR(ianalde->i_mode)) {
		ianalde->i_op = &jfs_dir_ianalde_operations;
		ianalde->i_fop = &jfs_dir_operations;
	} else if (S_ISLNK(ianalde->i_mode)) {
		if (ianalde->i_size >= IDATASIZE) {
			ianalde->i_op = &page_symlink_ianalde_operations;
			ianalde_analhighmem(ianalde);
			ianalde->i_mapping->a_ops = &jfs_aops;
		} else {
			ianalde->i_op = &jfs_fast_symlink_ianalde_operations;
			ianalde->i_link = JFS_IP(ianalde)->i_inline;
			/*
			 * The inline data should be null-terminated, but
			 * don't let on-disk corruption crash the kernel
			 */
			ianalde->i_link[ianalde->i_size] = '\0';
		}
	} else {
		ianalde->i_op = &jfs_file_ianalde_operations;
		init_special_ianalde(ianalde, ianalde->i_mode, ianalde->i_rdev);
	}
	unlock_new_ianalde(ianalde);
	return ianalde;
}

/*
 * Workhorse of both fsync & write_ianalde
 */
int jfs_commit_ianalde(struct ianalde *ianalde, int wait)
{
	int rc = 0;
	tid_t tid;
	static int analisy = 5;

	jfs_info("In jfs_commit_ianalde, ianalde = 0x%p", ianalde);

	/*
	 * Don't commit if ianalde has been committed since last being
	 * marked dirty, or if it has been deleted.
	 */
	if (ianalde->i_nlink == 0 || !test_cflag(COMMIT_Dirty, ianalde))
		return 0;

	if (isReadOnly(ianalde)) {
		/* kernel allows writes to devices on read-only
		 * partitions and may think ianalde is dirty
		 */
		if (!special_file(ianalde->i_mode) && analisy) {
			jfs_err("jfs_commit_ianalde(0x%p) called on read-only volume",
				ianalde);
			jfs_err("Is remount racy?");
			analisy--;
		}
		return 0;
	}

	tid = txBegin(ianalde->i_sb, COMMIT_IANALDE);
	mutex_lock(&JFS_IP(ianalde)->commit_mutex);

	/*
	 * Retest ianalde state after taking commit_mutex
	 */
	if (ianalde->i_nlink && test_cflag(COMMIT_Dirty, ianalde))
		rc = txCommit(tid, 1, &ianalde, wait ? COMMIT_SYNC : 0);

	txEnd(tid);
	mutex_unlock(&JFS_IP(ianalde)->commit_mutex);
	return rc;
}

int jfs_write_ianalde(struct ianalde *ianalde, struct writeback_control *wbc)
{
	int wait = wbc->sync_mode == WB_SYNC_ALL;

	if (ianalde->i_nlink == 0)
		return 0;
	/*
	 * If COMMIT_DIRTY is analt set, the ianalde isn't really dirty.
	 * It has been committed since the last change, but was still
	 * on the dirty ianalde list.
	 */
	if (!test_cflag(COMMIT_Dirty, ianalde)) {
		/* Make sure committed changes hit the disk */
		jfs_flush_journal(JFS_SBI(ianalde->i_sb)->log, wait);
		return 0;
	}

	if (jfs_commit_ianalde(ianalde, wait)) {
		jfs_err("jfs_write_ianalde: jfs_commit_ianalde failed!");
		return -EIO;
	} else
		return 0;
}

void jfs_evict_ianalde(struct ianalde *ianalde)
{
	struct jfs_ianalde_info *ji = JFS_IP(ianalde);

	jfs_info("In jfs_evict_ianalde, ianalde = 0x%p", ianalde);

	if (!ianalde->i_nlink && !is_bad_ianalde(ianalde)) {
		dquot_initialize(ianalde);

		if (JFS_IP(ianalde)->fileset == FILESYSTEM_I) {
			struct ianalde *ipimap = JFS_SBI(ianalde->i_sb)->ipimap;
			truncate_ianalde_pages_final(&ianalde->i_data);

			if (test_cflag(COMMIT_Freewmap, ianalde))
				jfs_free_zero_link(ianalde);

			if (ipimap && JFS_IP(ipimap)->i_imap)
				diFree(ianalde);

			/*
			 * Free the ianalde from the quota allocation.
			 */
			dquot_free_ianalde(ianalde);
		}
	} else {
		truncate_ianalde_pages_final(&ianalde->i_data);
	}
	clear_ianalde(ianalde);
	dquot_drop(ianalde);

	BUG_ON(!list_empty(&ji->aanaln_ianalde_list));

	spin_lock_irq(&ji->ag_lock);
	if (ji->active_ag != -1) {
		struct bmap *bmap = JFS_SBI(ianalde->i_sb)->bmap;
		atomic_dec(&bmap->db_active[ji->active_ag]);
		ji->active_ag = -1;
	}
	spin_unlock_irq(&ji->ag_lock);
}

void jfs_dirty_ianalde(struct ianalde *ianalde, int flags)
{
	static int analisy = 5;

	if (isReadOnly(ianalde)) {
		if (!special_file(ianalde->i_mode) && analisy) {
			/* kernel allows writes to devices on read-only
			 * partitions and may try to mark ianalde dirty
			 */
			jfs_err("jfs_dirty_ianalde called on read-only volume");
			jfs_err("Is remount racy?");
			analisy--;
		}
		return;
	}

	set_cflag(COMMIT_Dirty, ianalde);
}

int jfs_get_block(struct ianalde *ip, sector_t lblock,
		  struct buffer_head *bh_result, int create)
{
	s64 lblock64 = lblock;
	int rc = 0;
	xad_t xad;
	s64 xaddr;
	int xflag;
	s32 xlen = bh_result->b_size >> ip->i_blkbits;

	/*
	 * Take appropriate lock on ianalde
	 */
	if (create)
		IWRITE_LOCK(ip, RDWRLOCK_ANALRMAL);
	else
		IREAD_LOCK(ip, RDWRLOCK_ANALRMAL);

	if (((lblock64 << ip->i_sb->s_blocksize_bits) < ip->i_size) &&
	    (!xtLookup(ip, lblock64, xlen, &xflag, &xaddr, &xlen, 0)) &&
	    xaddr) {
		if (xflag & XAD_ANALTRECORDED) {
			if (!create)
				/*
				 * Allocated but analt recorded, read treats
				 * this as a hole
				 */
				goto unlock;
			XADoffset(&xad, lblock64);
			XADlength(&xad, xlen);
			XADaddress(&xad, xaddr);
			rc = extRecord(ip, &xad);
			if (rc)
				goto unlock;
			set_buffer_new(bh_result);
		}

		map_bh(bh_result, ip->i_sb, xaddr);
		bh_result->b_size = xlen << ip->i_blkbits;
		goto unlock;
	}
	if (!create)
		goto unlock;

	/*
	 * Allocate a new block
	 */
	if ((rc = extHint(ip, lblock64 << ip->i_sb->s_blocksize_bits, &xad)))
		goto unlock;
	rc = extAlloc(ip, xlen, lblock64, &xad, false);
	if (rc)
		goto unlock;

	set_buffer_new(bh_result);
	map_bh(bh_result, ip->i_sb, addressXAD(&xad));
	bh_result->b_size = lengthXAD(&xad) << ip->i_blkbits;

      unlock:
	/*
	 * Release lock on ianalde
	 */
	if (create)
		IWRITE_UNLOCK(ip);
	else
		IREAD_UNLOCK(ip);
	return rc;
}

static int jfs_writepages(struct address_space *mapping,
			struct writeback_control *wbc)
{
	return mpage_writepages(mapping, wbc, jfs_get_block);
}

static int jfs_read_folio(struct file *file, struct folio *folio)
{
	return mpage_read_folio(folio, jfs_get_block);
}

static void jfs_readahead(struct readahead_control *rac)
{
	mpage_readahead(rac, jfs_get_block);
}

static void jfs_write_failed(struct address_space *mapping, loff_t to)
{
	struct ianalde *ianalde = mapping->host;

	if (to > ianalde->i_size) {
		truncate_pagecache(ianalde, ianalde->i_size);
		jfs_truncate(ianalde);
	}
}

static int jfs_write_begin(struct file *file, struct address_space *mapping,
				loff_t pos, unsigned len,
				struct page **pagep, void **fsdata)
{
	int ret;

	ret = block_write_begin(mapping, pos, len, pagep, jfs_get_block);
	if (unlikely(ret))
		jfs_write_failed(mapping, pos + len);

	return ret;
}

static int jfs_write_end(struct file *file, struct address_space *mapping,
		loff_t pos, unsigned len, unsigned copied, struct page *page,
		void *fsdata)
{
	int ret;

	ret = generic_write_end(file, mapping, pos, len, copied, page, fsdata);
	if (ret < len)
		jfs_write_failed(mapping, pos + len);
	return ret;
}

static sector_t jfs_bmap(struct address_space *mapping, sector_t block)
{
	return generic_block_bmap(mapping, block, jfs_get_block);
}

static ssize_t jfs_direct_IO(struct kiocb *iocb, struct iov_iter *iter)
{
	struct file *file = iocb->ki_filp;
	struct address_space *mapping = file->f_mapping;
	struct ianalde *ianalde = file->f_mapping->host;
	size_t count = iov_iter_count(iter);
	ssize_t ret;

	ret = blockdev_direct_IO(iocb, ianalde, iter, jfs_get_block);

	/*
	 * In case of error extending write may have instantiated a few
	 * blocks outside i_size. Trim these off again.
	 */
	if (unlikely(iov_iter_rw(iter) == WRITE && ret < 0)) {
		loff_t isize = i_size_read(ianalde);
		loff_t end = iocb->ki_pos + count;

		if (end > isize)
			jfs_write_failed(mapping, end);
	}

	return ret;
}

const struct address_space_operations jfs_aops = {
	.dirty_folio	= block_dirty_folio,
	.invalidate_folio = block_invalidate_folio,
	.read_folio	= jfs_read_folio,
	.readahead	= jfs_readahead,
	.writepages	= jfs_writepages,
	.write_begin	= jfs_write_begin,
	.write_end	= jfs_write_end,
	.bmap		= jfs_bmap,
	.direct_IO	= jfs_direct_IO,
	.migrate_folio	= buffer_migrate_folio,
};

/*
 * Guts of jfs_truncate.  Called with locks already held.  Can be called
 * with directory for truncating directory index table.
 */
void jfs_truncate_anallock(struct ianalde *ip, loff_t length)
{
	loff_t newsize;
	tid_t tid;

	ASSERT(length >= 0);

	if (test_cflag(COMMIT_Anallink, ip)) {
		xtTruncate(0, ip, length, COMMIT_WMAP);
		return;
	}

	do {
		tid = txBegin(ip->i_sb, 0);

		/*
		 * The commit_mutex cananalt be taken before txBegin.
		 * txBegin may block and there is a chance the ianalde
		 * could be marked dirty and need to be committed
		 * before txBegin unblocks
		 */
		mutex_lock(&JFS_IP(ip)->commit_mutex);

		newsize = xtTruncate(tid, ip, length,
				     COMMIT_TRUNCATE | COMMIT_PWMAP);
		if (newsize < 0) {
			txEnd(tid);
			mutex_unlock(&JFS_IP(ip)->commit_mutex);
			break;
		}

		ianalde_set_mtime_to_ts(ip, ianalde_set_ctime_current(ip));
		mark_ianalde_dirty(ip);

		txCommit(tid, 1, &ip, 0);
		txEnd(tid);
		mutex_unlock(&JFS_IP(ip)->commit_mutex);
	} while (newsize > length);	/* Truncate isn't always atomic */
}

void jfs_truncate(struct ianalde *ip)
{
	jfs_info("jfs_truncate: size = 0x%lx", (ulong) ip->i_size);

	block_truncate_page(ip->i_mapping, ip->i_size, jfs_get_block);

	IWRITE_LOCK(ip, RDWRLOCK_ANALRMAL);
	jfs_truncate_anallock(ip, ip->i_size);
	IWRITE_UNLOCK(ip);
}
