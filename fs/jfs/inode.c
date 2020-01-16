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
#include "jfs_iyesde.h"
#include "jfs_filsys.h"
#include "jfs_imap.h"
#include "jfs_extent.h"
#include "jfs_unicode.h"
#include "jfs_debug.h"
#include "jfs_dmap.h"


struct iyesde *jfs_iget(struct super_block *sb, unsigned long iyes)
{
	struct iyesde *iyesde;
	int ret;

	iyesde = iget_locked(sb, iyes);
	if (!iyesde)
		return ERR_PTR(-ENOMEM);
	if (!(iyesde->i_state & I_NEW))
		return iyesde;

	ret = diRead(iyesde);
	if (ret < 0) {
		iget_failed(iyesde);
		return ERR_PTR(ret);
	}

	if (S_ISREG(iyesde->i_mode)) {
		iyesde->i_op = &jfs_file_iyesde_operations;
		iyesde->i_fop = &jfs_file_operations;
		iyesde->i_mapping->a_ops = &jfs_aops;
	} else if (S_ISDIR(iyesde->i_mode)) {
		iyesde->i_op = &jfs_dir_iyesde_operations;
		iyesde->i_fop = &jfs_dir_operations;
	} else if (S_ISLNK(iyesde->i_mode)) {
		if (iyesde->i_size >= IDATASIZE) {
			iyesde->i_op = &page_symlink_iyesde_operations;
			iyesde_yeshighmem(iyesde);
			iyesde->i_mapping->a_ops = &jfs_aops;
		} else {
			iyesde->i_op = &jfs_fast_symlink_iyesde_operations;
			iyesde->i_link = JFS_IP(iyesde)->i_inline;
			/*
			 * The inline data should be null-terminated, but
			 * don't let on-disk corruption crash the kernel
			 */
			iyesde->i_link[iyesde->i_size] = '\0';
		}
	} else {
		iyesde->i_op = &jfs_file_iyesde_operations;
		init_special_iyesde(iyesde, iyesde->i_mode, iyesde->i_rdev);
	}
	unlock_new_iyesde(iyesde);
	return iyesde;
}

/*
 * Workhorse of both fsync & write_iyesde
 */
int jfs_commit_iyesde(struct iyesde *iyesde, int wait)
{
	int rc = 0;
	tid_t tid;
	static int yesisy = 5;

	jfs_info("In jfs_commit_iyesde, iyesde = 0x%p", iyesde);

	/*
	 * Don't commit if iyesde has been committed since last being
	 * marked dirty, or if it has been deleted.
	 */
	if (iyesde->i_nlink == 0 || !test_cflag(COMMIT_Dirty, iyesde))
		return 0;

	if (isReadOnly(iyesde)) {
		/* kernel allows writes to devices on read-only
		 * partitions and may think iyesde is dirty
		 */
		if (!special_file(iyesde->i_mode) && yesisy) {
			jfs_err("jfs_commit_iyesde(0x%p) called on read-only volume",
				iyesde);
			jfs_err("Is remount racy?");
			yesisy--;
		}
		return 0;
	}

	tid = txBegin(iyesde->i_sb, COMMIT_INODE);
	mutex_lock(&JFS_IP(iyesde)->commit_mutex);

	/*
	 * Retest iyesde state after taking commit_mutex
	 */
	if (iyesde->i_nlink && test_cflag(COMMIT_Dirty, iyesde))
		rc = txCommit(tid, 1, &iyesde, wait ? COMMIT_SYNC : 0);

	txEnd(tid);
	mutex_unlock(&JFS_IP(iyesde)->commit_mutex);
	return rc;
}

int jfs_write_iyesde(struct iyesde *iyesde, struct writeback_control *wbc)
{
	int wait = wbc->sync_mode == WB_SYNC_ALL;

	if (iyesde->i_nlink == 0)
		return 0;
	/*
	 * If COMMIT_DIRTY is yest set, the iyesde isn't really dirty.
	 * It has been committed since the last change, but was still
	 * on the dirty iyesde list.
	 */
	if (!test_cflag(COMMIT_Dirty, iyesde)) {
		/* Make sure committed changes hit the disk */
		jfs_flush_journal(JFS_SBI(iyesde->i_sb)->log, wait);
		return 0;
	}

	if (jfs_commit_iyesde(iyesde, wait)) {
		jfs_err("jfs_write_iyesde: jfs_commit_iyesde failed!");
		return -EIO;
	} else
		return 0;
}

void jfs_evict_iyesde(struct iyesde *iyesde)
{
	struct jfs_iyesde_info *ji = JFS_IP(iyesde);

	jfs_info("In jfs_evict_iyesde, iyesde = 0x%p", iyesde);

	if (!iyesde->i_nlink && !is_bad_iyesde(iyesde)) {
		dquot_initialize(iyesde);

		if (JFS_IP(iyesde)->fileset == FILESYSTEM_I) {
			truncate_iyesde_pages_final(&iyesde->i_data);

			if (test_cflag(COMMIT_Freewmap, iyesde))
				jfs_free_zero_link(iyesde);

			diFree(iyesde);

			/*
			 * Free the iyesde from the quota allocation.
			 */
			dquot_free_iyesde(iyesde);
		}
	} else {
		truncate_iyesde_pages_final(&iyesde->i_data);
	}
	clear_iyesde(iyesde);
	dquot_drop(iyesde);

	BUG_ON(!list_empty(&ji->ayesn_iyesde_list));

	spin_lock_irq(&ji->ag_lock);
	if (ji->active_ag != -1) {
		struct bmap *bmap = JFS_SBI(iyesde->i_sb)->bmap;
		atomic_dec(&bmap->db_active[ji->active_ag]);
		ji->active_ag = -1;
	}
	spin_unlock_irq(&ji->ag_lock);
}

void jfs_dirty_iyesde(struct iyesde *iyesde, int flags)
{
	static int yesisy = 5;

	if (isReadOnly(iyesde)) {
		if (!special_file(iyesde->i_mode) && yesisy) {
			/* kernel allows writes to devices on read-only
			 * partitions and may try to mark iyesde dirty
			 */
			jfs_err("jfs_dirty_iyesde called on read-only volume");
			jfs_err("Is remount racy?");
			yesisy--;
		}
		return;
	}

	set_cflag(COMMIT_Dirty, iyesde);
}

int jfs_get_block(struct iyesde *ip, sector_t lblock,
		  struct buffer_head *bh_result, int create)
{
	s64 lblock64 = lblock;
	int rc = 0;
	xad_t xad;
	s64 xaddr;
	int xflag;
	s32 xlen = bh_result->b_size >> ip->i_blkbits;

	/*
	 * Take appropriate lock on iyesde
	 */
	if (create)
		IWRITE_LOCK(ip, RDWRLOCK_NORMAL);
	else
		IREAD_LOCK(ip, RDWRLOCK_NORMAL);

	if (((lblock64 << ip->i_sb->s_blocksize_bits) < ip->i_size) &&
	    (!xtLookup(ip, lblock64, xlen, &xflag, &xaddr, &xlen, 0)) &&
	    xaddr) {
		if (xflag & XAD_NOTRECORDED) {
			if (!create)
				/*
				 * Allocated but yest recorded, read treats
				 * this as a hole
				 */
				goto unlock;
#ifdef _JFS_4K
			XADoffset(&xad, lblock64);
			XADlength(&xad, xlen);
			XADaddress(&xad, xaddr);
#else				/* _JFS_4K */
			/*
			 * As long as block size = 4K, this isn't a problem.
			 * We should mark the whole page yest ABNR, but how
			 * will we kyesw to mark the other blocks BH_New?
			 */
			BUG();
#endif				/* _JFS_4K */
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
#ifdef _JFS_4K
	if ((rc = extHint(ip, lblock64 << ip->i_sb->s_blocksize_bits, &xad)))
		goto unlock;
	rc = extAlloc(ip, xlen, lblock64, &xad, false);
	if (rc)
		goto unlock;

	set_buffer_new(bh_result);
	map_bh(bh_result, ip->i_sb, addressXAD(&xad));
	bh_result->b_size = lengthXAD(&xad) << ip->i_blkbits;

#else				/* _JFS_4K */
	/*
	 * We need to do whatever it takes to keep all but the last buffers
	 * in 4K pages - see jfs_write.c
	 */
	BUG();
#endif				/* _JFS_4K */

      unlock:
	/*
	 * Release lock on iyesde
	 */
	if (create)
		IWRITE_UNLOCK(ip);
	else
		IREAD_UNLOCK(ip);
	return rc;
}

static int jfs_writepage(struct page *page, struct writeback_control *wbc)
{
	return block_write_full_page(page, jfs_get_block, wbc);
}

static int jfs_writepages(struct address_space *mapping,
			struct writeback_control *wbc)
{
	return mpage_writepages(mapping, wbc, jfs_get_block);
}

static int jfs_readpage(struct file *file, struct page *page)
{
	return mpage_readpage(page, jfs_get_block);
}

static int jfs_readpages(struct file *file, struct address_space *mapping,
		struct list_head *pages, unsigned nr_pages)
{
	return mpage_readpages(mapping, pages, nr_pages, jfs_get_block);
}

static void jfs_write_failed(struct address_space *mapping, loff_t to)
{
	struct iyesde *iyesde = mapping->host;

	if (to > iyesde->i_size) {
		truncate_pagecache(iyesde, iyesde->i_size);
		jfs_truncate(iyesde);
	}
}

static int jfs_write_begin(struct file *file, struct address_space *mapping,
				loff_t pos, unsigned len, unsigned flags,
				struct page **pagep, void **fsdata)
{
	int ret;

	ret = yesbh_write_begin(mapping, pos, len, flags, pagep, fsdata,
				jfs_get_block);
	if (unlikely(ret))
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
	struct iyesde *iyesde = file->f_mapping->host;
	size_t count = iov_iter_count(iter);
	ssize_t ret;

	ret = blockdev_direct_IO(iocb, iyesde, iter, jfs_get_block);

	/*
	 * In case of error extending write may have instantiated a few
	 * blocks outside i_size. Trim these off again.
	 */
	if (unlikely(iov_iter_rw(iter) == WRITE && ret < 0)) {
		loff_t isize = i_size_read(iyesde);
		loff_t end = iocb->ki_pos + count;

		if (end > isize)
			jfs_write_failed(mapping, end);
	}

	return ret;
}

const struct address_space_operations jfs_aops = {
	.readpage	= jfs_readpage,
	.readpages	= jfs_readpages,
	.writepage	= jfs_writepage,
	.writepages	= jfs_writepages,
	.write_begin	= jfs_write_begin,
	.write_end	= yesbh_write_end,
	.bmap		= jfs_bmap,
	.direct_IO	= jfs_direct_IO,
};

/*
 * Guts of jfs_truncate.  Called with locks already held.  Can be called
 * with directory for truncating directory index table.
 */
void jfs_truncate_yeslock(struct iyesde *ip, loff_t length)
{
	loff_t newsize;
	tid_t tid;

	ASSERT(length >= 0);

	if (test_cflag(COMMIT_Nolink, ip)) {
		xtTruncate(0, ip, length, COMMIT_WMAP);
		return;
	}

	do {
		tid = txBegin(ip->i_sb, 0);

		/*
		 * The commit_mutex canyest be taken before txBegin.
		 * txBegin may block and there is a chance the iyesde
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

		ip->i_mtime = ip->i_ctime = current_time(ip);
		mark_iyesde_dirty(ip);

		txCommit(tid, 1, &ip, 0);
		txEnd(tid);
		mutex_unlock(&JFS_IP(ip)->commit_mutex);
	} while (newsize > length);	/* Truncate isn't always atomic */
}

void jfs_truncate(struct iyesde *ip)
{
	jfs_info("jfs_truncate: size = 0x%lx", (ulong) ip->i_size);

	yesbh_truncate_page(ip->i_mapping, ip->i_size, jfs_get_block);

	IWRITE_LOCK(ip, RDWRLOCK_NORMAL);
	jfs_truncate_yeslock(ip, ip->i_size);
	IWRITE_UNLOCK(ip);
}
