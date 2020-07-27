// SPDX-License-Identifier: GPL-2.0
/*
 * Simple file system for zoned block devices exposing zones as files.
 *
 * Copyright (C) 2019 Western Digital Corporation or its affiliates.
 */
#include <linux/module.h>
#include <linux/fs.h>
#include <linux/magic.h>
#include <linux/iomap.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/blkdev.h>
#include <linux/statfs.h>
#include <linux/writeback.h>
#include <linux/quotaops.h>
#include <linux/seq_file.h>
#include <linux/parser.h>
#include <linux/uio.h>
#include <linux/mman.h>
#include <linux/sched/mm.h>
#include <linux/crc32.h>
#include <linux/task_io_accounting_ops.h>

#include "zonefs.h"

static int zonefs_iomap_begin(struct inode *inode, loff_t offset, loff_t length,
			      unsigned int flags, struct iomap *iomap,
			      struct iomap *srcmap)
{
	struct zonefs_inode_info *zi = ZONEFS_I(inode);
	struct super_block *sb = inode->i_sb;
	loff_t isize;

	/* All I/Os should always be within the file maximum size */
	if (WARN_ON_ONCE(offset + length > zi->i_max_size))
		return -EIO;

	/*
	 * Sequential zones can only accept direct writes. This is already
	 * checked when writes are issued, so warn if we see a page writeback
	 * operation.
	 */
	if (WARN_ON_ONCE(zi->i_ztype == ZONEFS_ZTYPE_SEQ &&
			 (flags & IOMAP_WRITE) && !(flags & IOMAP_DIRECT)))
		return -EIO;

	/*
	 * For conventional zones, all blocks are always mapped. For sequential
	 * zones, all blocks after always mapped below the inode size (zone
	 * write pointer) and unwriten beyond.
	 */
	mutex_lock(&zi->i_truncate_mutex);
	isize = i_size_read(inode);
	if (offset >= isize)
		iomap->type = IOMAP_UNWRITTEN;
	else
		iomap->type = IOMAP_MAPPED;
	if (flags & IOMAP_WRITE)
		length = zi->i_max_size - offset;
	else
		length = min(length, isize - offset);
	mutex_unlock(&zi->i_truncate_mutex);

	iomap->offset = ALIGN_DOWN(offset, sb->s_blocksize);
	iomap->length = ALIGN(offset + length, sb->s_blocksize) - iomap->offset;
	iomap->bdev = inode->i_sb->s_bdev;
	iomap->addr = (zi->i_zsector << SECTOR_SHIFT) + iomap->offset;

	return 0;
}

static const struct iomap_ops zonefs_iomap_ops = {
	.iomap_begin	= zonefs_iomap_begin,
};

static int zonefs_readpage(struct file *unused, struct page *page)
{
	return iomap_readpage(page, &zonefs_iomap_ops);
}

static void zonefs_readahead(struct readahead_control *rac)
{
	iomap_readahead(rac, &zonefs_iomap_ops);
}

/*
 * Map blocks for page writeback. This is used only on conventional zone files,
 * which implies that the page range can only be within the fixed inode size.
 */
static int zonefs_map_blocks(struct iomap_writepage_ctx *wpc,
			     struct inode *inode, loff_t offset)
{
	struct zonefs_inode_info *zi = ZONEFS_I(inode);

	if (WARN_ON_ONCE(zi->i_ztype != ZONEFS_ZTYPE_CNV))
		return -EIO;
	if (WARN_ON_ONCE(offset >= i_size_read(inode)))
		return -EIO;

	/* If the mapping is already OK, nothing needs to be done */
	if (offset >= wpc->iomap.offset &&
	    offset < wpc->iomap.offset + wpc->iomap.length)
		return 0;

	return zonefs_iomap_begin(inode, offset, zi->i_max_size - offset,
				  IOMAP_WRITE, &wpc->iomap, NULL);
}

static const struct iomap_writeback_ops zonefs_writeback_ops = {
	.map_blocks		= zonefs_map_blocks,
};

static int zonefs_writepage(struct page *page, struct writeback_control *wbc)
{
	struct iomap_writepage_ctx wpc = { };

	return iomap_writepage(page, wbc, &wpc, &zonefs_writeback_ops);
}

static int zonefs_writepages(struct address_space *mapping,
			     struct writeback_control *wbc)
{
	struct iomap_writepage_ctx wpc = { };

	return iomap_writepages(mapping, wbc, &wpc, &zonefs_writeback_ops);
}

static const struct address_space_operations zonefs_file_aops = {
	.readpage		= zonefs_readpage,
	.readahead		= zonefs_readahead,
	.writepage		= zonefs_writepage,
	.writepages		= zonefs_writepages,
	.set_page_dirty		= iomap_set_page_dirty,
	.releasepage		= iomap_releasepage,
	.invalidatepage		= iomap_invalidatepage,
	.migratepage		= iomap_migrate_page,
	.is_partially_uptodate	= iomap_is_partially_uptodate,
	.error_remove_page	= generic_error_remove_page,
	.direct_IO		= noop_direct_IO,
};

static void zonefs_update_stats(struct inode *inode, loff_t new_isize)
{
	struct super_block *sb = inode->i_sb;
	struct zonefs_sb_info *sbi = ZONEFS_SB(sb);
	loff_t old_isize = i_size_read(inode);
	loff_t nr_blocks;

	if (new_isize == old_isize)
		return;

	spin_lock(&sbi->s_lock);

	/*
	 * This may be called for an update after an IO error.
	 * So beware of the values seen.
	 */
	if (new_isize < old_isize) {
		nr_blocks = (old_isize - new_isize) >> sb->s_blocksize_bits;
		if (sbi->s_used_blocks > nr_blocks)
			sbi->s_used_blocks -= nr_blocks;
		else
			sbi->s_used_blocks = 0;
	} else {
		sbi->s_used_blocks +=
			(new_isize - old_isize) >> sb->s_blocksize_bits;
		if (sbi->s_used_blocks > sbi->s_blocks)
			sbi->s_used_blocks = sbi->s_blocks;
	}

	spin_unlock(&sbi->s_lock);
}

/*
 * Check a zone condition and adjust its file inode access permissions for
 * offline and readonly zones. Return the inode size corresponding to the
 * amount of readable data in the zone.
 */
static loff_t zonefs_check_zone_condition(struct inode *inode,
					  struct blk_zone *zone, bool warn,
					  bool mount)
{
	struct zonefs_inode_info *zi = ZONEFS_I(inode);

	switch (zone->cond) {
	case BLK_ZONE_COND_OFFLINE:
		/*
		 * Dead zone: make the inode immutable, disable all accesses
		 * and set the file size to 0 (zone wp set to zone start).
		 */
		if (warn)
			zonefs_warn(inode->i_sb, "inode %lu: offline zone\n",
				    inode->i_ino);
		inode->i_flags |= S_IMMUTABLE;
		inode->i_mode &= ~0777;
		zone->wp = zone->start;
		return 0;
	case BLK_ZONE_COND_READONLY:
		/*
		 * The write pointer of read-only zones is invalid. If such a
		 * zone is found during mount, the file size cannot be retrieved
		 * so we treat the zone as offline (mount == true case).
		 * Otherwise, keep the file size as it was when last updated
		 * so that the user can recover data. In both cases, writes are
		 * always disabled for the zone.
		 */
		if (warn)
			zonefs_warn(inode->i_sb, "inode %lu: read-only zone\n",
				    inode->i_ino);
		inode->i_flags |= S_IMMUTABLE;
		if (mount) {
			zone->cond = BLK_ZONE_COND_OFFLINE;
			inode->i_mode &= ~0777;
			zone->wp = zone->start;
			return 0;
		}
		inode->i_mode &= ~0222;
		return i_size_read(inode);
	default:
		if (zi->i_ztype == ZONEFS_ZTYPE_CNV)
			return zi->i_max_size;
		return (zone->wp - zone->start) << SECTOR_SHIFT;
	}
}

struct zonefs_ioerr_data {
	struct inode	*inode;
	bool		write;
};

static int zonefs_io_error_cb(struct blk_zone *zone, unsigned int idx,
			      void *data)
{
	struct zonefs_ioerr_data *err = data;
	struct inode *inode = err->inode;
	struct zonefs_inode_info *zi = ZONEFS_I(inode);
	struct super_block *sb = inode->i_sb;
	struct zonefs_sb_info *sbi = ZONEFS_SB(sb);
	loff_t isize, data_size;

	/*
	 * Check the zone condition: if the zone is not "bad" (offline or
	 * read-only), read errors are simply signaled to the IO issuer as long
	 * as there is no inconsistency between the inode size and the amount of
	 * data writen in the zone (data_size).
	 */
	data_size = zonefs_check_zone_condition(inode, zone, true, false);
	isize = i_size_read(inode);
	if (zone->cond != BLK_ZONE_COND_OFFLINE &&
	    zone->cond != BLK_ZONE_COND_READONLY &&
	    !err->write && isize == data_size)
		return 0;

	/*
	 * At this point, we detected either a bad zone or an inconsistency
	 * between the inode size and the amount of data written in the zone.
	 * For the latter case, the cause may be a write IO error or an external
	 * action on the device. Two error patterns exist:
	 * 1) The inode size is lower than the amount of data in the zone:
	 *    a write operation partially failed and data was writen at the end
	 *    of the file. This can happen in the case of a large direct IO
	 *    needing several BIOs and/or write requests to be processed.
	 * 2) The inode size is larger than the amount of data in the zone:
	 *    this can happen with a deferred write error with the use of the
	 *    device side write cache after getting successful write IO
	 *    completions. Other possibilities are (a) an external corruption,
	 *    e.g. an application reset the zone directly, or (b) the device
	 *    has a serious problem (e.g. firmware bug).
	 *
	 * In all cases, warn about inode size inconsistency and handle the
	 * IO error according to the zone condition and to the mount options.
	 */
	if (zi->i_ztype == ZONEFS_ZTYPE_SEQ && isize != data_size)
		zonefs_warn(sb, "inode %lu: invalid size %lld (should be %lld)\n",
			    inode->i_ino, isize, data_size);

	/*
	 * First handle bad zones signaled by hardware. The mount options
	 * errors=zone-ro and errors=zone-offline result in changing the
	 * zone condition to read-only and offline respectively, as if the
	 * condition was signaled by the hardware.
	 */
	if (zone->cond == BLK_ZONE_COND_OFFLINE ||
	    sbi->s_mount_opts & ZONEFS_MNTOPT_ERRORS_ZOL) {
		zonefs_warn(sb, "inode %lu: read/write access disabled\n",
			    inode->i_ino);
		if (zone->cond != BLK_ZONE_COND_OFFLINE) {
			zone->cond = BLK_ZONE_COND_OFFLINE;
			data_size = zonefs_check_zone_condition(inode, zone,
								false, false);
		}
	} else if (zone->cond == BLK_ZONE_COND_READONLY ||
		   sbi->s_mount_opts & ZONEFS_MNTOPT_ERRORS_ZRO) {
		zonefs_warn(sb, "inode %lu: write access disabled\n",
			    inode->i_ino);
		if (zone->cond != BLK_ZONE_COND_READONLY) {
			zone->cond = BLK_ZONE_COND_READONLY;
			data_size = zonefs_check_zone_condition(inode, zone,
								false, false);
		}
	}

	/*
	 * If error=remount-ro was specified, any error result in remounting
	 * the volume as read-only.
	 */
	if ((sbi->s_mount_opts & ZONEFS_MNTOPT_ERRORS_RO) && !sb_rdonly(sb)) {
		zonefs_warn(sb, "remounting filesystem read-only\n");
		sb->s_flags |= SB_RDONLY;
	}

	/*
	 * Update block usage stats and the inode size  to prevent access to
	 * invalid data.
	 */
	zonefs_update_stats(inode, data_size);
	i_size_write(inode, data_size);
	zi->i_wpoffset = data_size;

	return 0;
}

/*
 * When an file IO error occurs, check the file zone to see if there is a change
 * in the zone condition (e.g. offline or read-only). For a failed write to a
 * sequential zone, the zone write pointer position must also be checked to
 * eventually correct the file size and zonefs inode write pointer offset
 * (which can be out of sync with the drive due to partial write failures).
 */
static void zonefs_io_error(struct inode *inode, bool write)
{
	struct zonefs_inode_info *zi = ZONEFS_I(inode);
	struct super_block *sb = inode->i_sb;
	struct zonefs_sb_info *sbi = ZONEFS_SB(sb);
	unsigned int noio_flag;
	unsigned int nr_zones =
		zi->i_max_size >> (sbi->s_zone_sectors_shift + SECTOR_SHIFT);
	struct zonefs_ioerr_data err = {
		.inode = inode,
		.write = write,
	};
	int ret;

	mutex_lock(&zi->i_truncate_mutex);

	/*
	 * Memory allocations in blkdev_report_zones() can trigger a memory
	 * reclaim which may in turn cause a recursion into zonefs as well as
	 * struct request allocations for the same device. The former case may
	 * end up in a deadlock on the inode truncate mutex, while the latter
	 * may prevent IO forward progress. Executing the report zones under
	 * the GFP_NOIO context avoids both problems.
	 */
	noio_flag = memalloc_noio_save();
	ret = blkdev_report_zones(sb->s_bdev, zi->i_zsector, nr_zones,
				  zonefs_io_error_cb, &err);
	if (ret != nr_zones)
		zonefs_err(sb, "Get inode %lu zone information failed %d\n",
			   inode->i_ino, ret);
	memalloc_noio_restore(noio_flag);

	mutex_unlock(&zi->i_truncate_mutex);
}

static int zonefs_file_truncate(struct inode *inode, loff_t isize)
{
	struct zonefs_inode_info *zi = ZONEFS_I(inode);
	loff_t old_isize;
	enum req_opf op;
	int ret = 0;

	/*
	 * Only sequential zone files can be truncated and truncation is allowed
	 * only down to a 0 size, which is equivalent to a zone reset, and to
	 * the maximum file size, which is equivalent to a zone finish.
	 */
	if (zi->i_ztype != ZONEFS_ZTYPE_SEQ)
		return -EPERM;

	if (!isize)
		op = REQ_OP_ZONE_RESET;
	else if (isize == zi->i_max_size)
		op = REQ_OP_ZONE_FINISH;
	else
		return -EPERM;

	inode_dio_wait(inode);

	/* Serialize against page faults */
	down_write(&zi->i_mmap_sem);

	/* Serialize against zonefs_iomap_begin() */
	mutex_lock(&zi->i_truncate_mutex);

	old_isize = i_size_read(inode);
	if (isize == old_isize)
		goto unlock;

	ret = blkdev_zone_mgmt(inode->i_sb->s_bdev, op, zi->i_zsector,
			       zi->i_max_size >> SECTOR_SHIFT, GFP_NOFS);
	if (ret) {
		zonefs_err(inode->i_sb,
			   "Zone management operation at %llu failed %d",
			   zi->i_zsector, ret);
		goto unlock;
	}

	zonefs_update_stats(inode, isize);
	truncate_setsize(inode, isize);
	zi->i_wpoffset = isize;

unlock:
	mutex_unlock(&zi->i_truncate_mutex);
	up_write(&zi->i_mmap_sem);

	return ret;
}

static int zonefs_inode_setattr(struct dentry *dentry, struct iattr *iattr)
{
	struct inode *inode = d_inode(dentry);
	int ret;

	if (unlikely(IS_IMMUTABLE(inode)))
		return -EPERM;

	ret = setattr_prepare(dentry, iattr);
	if (ret)
		return ret;

	/*
	 * Since files and directories cannot be created nor deleted, do not
	 * allow setting any write attributes on the sub-directories grouping
	 * files by zone type.
	 */
	if ((iattr->ia_valid & ATTR_MODE) && S_ISDIR(inode->i_mode) &&
	    (iattr->ia_mode & 0222))
		return -EPERM;

	if (((iattr->ia_valid & ATTR_UID) &&
	     !uid_eq(iattr->ia_uid, inode->i_uid)) ||
	    ((iattr->ia_valid & ATTR_GID) &&
	     !gid_eq(iattr->ia_gid, inode->i_gid))) {
		ret = dquot_transfer(inode, iattr);
		if (ret)
			return ret;
	}

	if (iattr->ia_valid & ATTR_SIZE) {
		ret = zonefs_file_truncate(inode, iattr->ia_size);
		if (ret)
			return ret;
	}

	setattr_copy(inode, iattr);

	return 0;
}

static const struct inode_operations zonefs_file_inode_operations = {
	.setattr	= zonefs_inode_setattr,
};

static int zonefs_file_fsync(struct file *file, loff_t start, loff_t end,
			     int datasync)
{
	struct inode *inode = file_inode(file);
	int ret = 0;

	if (unlikely(IS_IMMUTABLE(inode)))
		return -EPERM;

	/*
	 * Since only direct writes are allowed in sequential files, page cache
	 * flush is needed only for conventional zone files.
	 */
	if (ZONEFS_I(inode)->i_ztype == ZONEFS_ZTYPE_CNV)
		ret = file_write_and_wait_range(file, start, end);
	if (!ret)
		ret = blkdev_issue_flush(inode->i_sb->s_bdev, GFP_KERNEL);

	if (ret)
		zonefs_io_error(inode, true);

	return ret;
}

static vm_fault_t zonefs_filemap_fault(struct vm_fault *vmf)
{
	struct zonefs_inode_info *zi = ZONEFS_I(file_inode(vmf->vma->vm_file));
	vm_fault_t ret;

	down_read(&zi->i_mmap_sem);
	ret = filemap_fault(vmf);
	up_read(&zi->i_mmap_sem);

	return ret;
}

static vm_fault_t zonefs_filemap_page_mkwrite(struct vm_fault *vmf)
{
	struct inode *inode = file_inode(vmf->vma->vm_file);
	struct zonefs_inode_info *zi = ZONEFS_I(inode);
	vm_fault_t ret;

	if (unlikely(IS_IMMUTABLE(inode)))
		return VM_FAULT_SIGBUS;

	/*
	 * Sanity check: only conventional zone files can have shared
	 * writeable mappings.
	 */
	if (WARN_ON_ONCE(zi->i_ztype != ZONEFS_ZTYPE_CNV))
		return VM_FAULT_NOPAGE;

	sb_start_pagefault(inode->i_sb);
	file_update_time(vmf->vma->vm_file);

	/* Serialize against truncates */
	down_read(&zi->i_mmap_sem);
	ret = iomap_page_mkwrite(vmf, &zonefs_iomap_ops);
	up_read(&zi->i_mmap_sem);

	sb_end_pagefault(inode->i_sb);
	return ret;
}

static const struct vm_operations_struct zonefs_file_vm_ops = {
	.fault		= zonefs_filemap_fault,
	.map_pages	= filemap_map_pages,
	.page_mkwrite	= zonefs_filemap_page_mkwrite,
};

static int zonefs_file_mmap(struct file *file, struct vm_area_struct *vma)
{
	/*
	 * Conventional zones accept random writes, so their files can support
	 * shared writable mappings. For sequential zone files, only read
	 * mappings are possible since there are no guarantees for write
	 * ordering between msync() and page cache writeback.
	 */
	if (ZONEFS_I(file_inode(file))->i_ztype == ZONEFS_ZTYPE_SEQ &&
	    (vma->vm_flags & VM_SHARED) && (vma->vm_flags & VM_MAYWRITE))
		return -EINVAL;

	file_accessed(file);
	vma->vm_ops = &zonefs_file_vm_ops;

	return 0;
}

static loff_t zonefs_file_llseek(struct file *file, loff_t offset, int whence)
{
	loff_t isize = i_size_read(file_inode(file));

	/*
	 * Seeks are limited to below the zone size for conventional zones
	 * and below the zone write pointer for sequential zones. In both
	 * cases, this limit is the inode size.
	 */
	return generic_file_llseek_size(file, offset, whence, isize, isize);
}

static int zonefs_file_write_dio_end_io(struct kiocb *iocb, ssize_t size,
					int error, unsigned int flags)
{
	struct inode *inode = file_inode(iocb->ki_filp);
	struct zonefs_inode_info *zi = ZONEFS_I(inode);

	if (error) {
		zonefs_io_error(inode, true);
		return error;
	}

	if (size && zi->i_ztype != ZONEFS_ZTYPE_CNV) {
		/*
		 * Note that we may be seeing completions out of order,
		 * but that is not a problem since a write completed
		 * successfully necessarily means that all preceding writes
		 * were also successful. So we can safely increase the inode
		 * size to the write end location.
		 */
		mutex_lock(&zi->i_truncate_mutex);
		if (i_size_read(inode) < iocb->ki_pos + size) {
			zonefs_update_stats(inode, iocb->ki_pos + size);
			i_size_write(inode, iocb->ki_pos + size);
		}
		mutex_unlock(&zi->i_truncate_mutex);
	}

	return 0;
}

static const struct iomap_dio_ops zonefs_write_dio_ops = {
	.end_io			= zonefs_file_write_dio_end_io,
};

static ssize_t zonefs_file_dio_append(struct kiocb *iocb, struct iov_iter *from)
{
	struct inode *inode = file_inode(iocb->ki_filp);
	struct zonefs_inode_info *zi = ZONEFS_I(inode);
	struct block_device *bdev = inode->i_sb->s_bdev;
	unsigned int max;
	struct bio *bio;
	ssize_t size;
	int nr_pages;
	ssize_t ret;

	max = queue_max_zone_append_sectors(bdev_get_queue(bdev));
	max = ALIGN_DOWN(max << SECTOR_SHIFT, inode->i_sb->s_blocksize);
	iov_iter_truncate(from, max);

	nr_pages = iov_iter_npages(from, BIO_MAX_PAGES);
	if (!nr_pages)
		return 0;

	bio = bio_alloc_bioset(GFP_NOFS, nr_pages, &fs_bio_set);
	if (!bio)
		return -ENOMEM;

	bio_set_dev(bio, bdev);
	bio->bi_iter.bi_sector = zi->i_zsector;
	bio->bi_write_hint = iocb->ki_hint;
	bio->bi_ioprio = iocb->ki_ioprio;
	bio->bi_opf = REQ_OP_ZONE_APPEND | REQ_SYNC | REQ_IDLE;
	if (iocb->ki_flags & IOCB_DSYNC)
		bio->bi_opf |= REQ_FUA;

	ret = bio_iov_iter_get_pages(bio, from);
	if (unlikely(ret)) {
		bio_io_error(bio);
		return ret;
	}
	size = bio->bi_iter.bi_size;
	task_io_account_write(ret);

	if (iocb->ki_flags & IOCB_HIPRI)
		bio_set_polled(bio, iocb);

	ret = submit_bio_wait(bio);

	bio_put(bio);

	zonefs_file_write_dio_end_io(iocb, size, ret, 0);
	if (ret >= 0) {
		iocb->ki_pos += size;
		return size;
	}

	return ret;
}

/*
 * Handle direct writes. For sequential zone files, this is the only possible
 * write path. For these files, check that the user is issuing writes
 * sequentially from the end of the file. This code assumes that the block layer
 * delivers write requests to the device in sequential order. This is always the
 * case if a block IO scheduler implementing the ELEVATOR_F_ZBD_SEQ_WRITE
 * elevator feature is being used (e.g. mq-deadline). The block layer always
 * automatically select such an elevator for zoned block devices during the
 * device initialization.
 */
static ssize_t zonefs_file_dio_write(struct kiocb *iocb, struct iov_iter *from)
{
	struct inode *inode = file_inode(iocb->ki_filp);
	struct zonefs_inode_info *zi = ZONEFS_I(inode);
	struct super_block *sb = inode->i_sb;
	bool sync = is_sync_kiocb(iocb);
	bool append = false;
	size_t count;
	ssize_t ret;

	/*
	 * For async direct IOs to sequential zone files, refuse IOCB_NOWAIT
	 * as this can cause write reordering (e.g. the first aio gets EAGAIN
	 * on the inode lock but the second goes through but is now unaligned).
	 */
	if (zi->i_ztype == ZONEFS_ZTYPE_SEQ && !sync &&
	    (iocb->ki_flags & IOCB_NOWAIT))
		return -EOPNOTSUPP;

	if (iocb->ki_flags & IOCB_NOWAIT) {
		if (!inode_trylock(inode))
			return -EAGAIN;
	} else {
		inode_lock(inode);
	}

	ret = generic_write_checks(iocb, from);
	if (ret <= 0)
		goto inode_unlock;

	iov_iter_truncate(from, zi->i_max_size - iocb->ki_pos);
	count = iov_iter_count(from);

	if ((iocb->ki_pos | count) & (sb->s_blocksize - 1)) {
		ret = -EINVAL;
		goto inode_unlock;
	}

	/* Enforce sequential writes (append only) in sequential zones */
	if (zi->i_ztype == ZONEFS_ZTYPE_SEQ) {
		mutex_lock(&zi->i_truncate_mutex);
		if (iocb->ki_pos != zi->i_wpoffset) {
			mutex_unlock(&zi->i_truncate_mutex);
			ret = -EINVAL;
			goto inode_unlock;
		}
		mutex_unlock(&zi->i_truncate_mutex);
		append = sync;
	}

	if (append)
		ret = zonefs_file_dio_append(iocb, from);
	else
		ret = iomap_dio_rw(iocb, from, &zonefs_iomap_ops,
				   &zonefs_write_dio_ops, sync);
	if (zi->i_ztype == ZONEFS_ZTYPE_SEQ &&
	    (ret > 0 || ret == -EIOCBQUEUED)) {
		if (ret > 0)
			count = ret;
		mutex_lock(&zi->i_truncate_mutex);
		zi->i_wpoffset += count;
		mutex_unlock(&zi->i_truncate_mutex);
	}

inode_unlock:
	inode_unlock(inode);

	return ret;
}

static ssize_t zonefs_file_buffered_write(struct kiocb *iocb,
					  struct iov_iter *from)
{
	struct inode *inode = file_inode(iocb->ki_filp);
	struct zonefs_inode_info *zi = ZONEFS_I(inode);
	ssize_t ret;

	/*
	 * Direct IO writes are mandatory for sequential zone files so that the
	 * write IO issuing order is preserved.
	 */
	if (zi->i_ztype != ZONEFS_ZTYPE_CNV)
		return -EIO;

	if (iocb->ki_flags & IOCB_NOWAIT) {
		if (!inode_trylock(inode))
			return -EAGAIN;
	} else {
		inode_lock(inode);
	}

	ret = generic_write_checks(iocb, from);
	if (ret <= 0)
		goto inode_unlock;

	iov_iter_truncate(from, zi->i_max_size - iocb->ki_pos);

	ret = iomap_file_buffered_write(iocb, from, &zonefs_iomap_ops);
	if (ret > 0)
		iocb->ki_pos += ret;
	else if (ret == -EIO)
		zonefs_io_error(inode, true);

inode_unlock:
	inode_unlock(inode);
	if (ret > 0)
		ret = generic_write_sync(iocb, ret);

	return ret;
}

static ssize_t zonefs_file_write_iter(struct kiocb *iocb, struct iov_iter *from)
{
	struct inode *inode = file_inode(iocb->ki_filp);

	if (unlikely(IS_IMMUTABLE(inode)))
		return -EPERM;

	if (sb_rdonly(inode->i_sb))
		return -EROFS;

	/* Write operations beyond the zone size are not allowed */
	if (iocb->ki_pos >= ZONEFS_I(inode)->i_max_size)
		return -EFBIG;

	if (iocb->ki_flags & IOCB_DIRECT)
		return zonefs_file_dio_write(iocb, from);

	return zonefs_file_buffered_write(iocb, from);
}

static int zonefs_file_read_dio_end_io(struct kiocb *iocb, ssize_t size,
				       int error, unsigned int flags)
{
	if (error) {
		zonefs_io_error(file_inode(iocb->ki_filp), false);
		return error;
	}

	return 0;
}

static const struct iomap_dio_ops zonefs_read_dio_ops = {
	.end_io			= zonefs_file_read_dio_end_io,
};

static ssize_t zonefs_file_read_iter(struct kiocb *iocb, struct iov_iter *to)
{
	struct inode *inode = file_inode(iocb->ki_filp);
	struct zonefs_inode_info *zi = ZONEFS_I(inode);
	struct super_block *sb = inode->i_sb;
	loff_t isize;
	ssize_t ret;

	/* Offline zones cannot be read */
	if (unlikely(IS_IMMUTABLE(inode) && !(inode->i_mode & 0777)))
		return -EPERM;

	if (iocb->ki_pos >= zi->i_max_size)
		return 0;

	if (iocb->ki_flags & IOCB_NOWAIT) {
		if (!inode_trylock_shared(inode))
			return -EAGAIN;
	} else {
		inode_lock_shared(inode);
	}

	/* Limit read operations to written data */
	mutex_lock(&zi->i_truncate_mutex);
	isize = i_size_read(inode);
	if (iocb->ki_pos >= isize) {
		mutex_unlock(&zi->i_truncate_mutex);
		ret = 0;
		goto inode_unlock;
	}
	iov_iter_truncate(to, isize - iocb->ki_pos);
	mutex_unlock(&zi->i_truncate_mutex);

	if (iocb->ki_flags & IOCB_DIRECT) {
		size_t count = iov_iter_count(to);

		if ((iocb->ki_pos | count) & (sb->s_blocksize - 1)) {
			ret = -EINVAL;
			goto inode_unlock;
		}
		file_accessed(iocb->ki_filp);
		ret = iomap_dio_rw(iocb, to, &zonefs_iomap_ops,
				   &zonefs_read_dio_ops, is_sync_kiocb(iocb));
	} else {
		ret = generic_file_read_iter(iocb, to);
		if (ret == -EIO)
			zonefs_io_error(inode, false);
	}

inode_unlock:
	inode_unlock_shared(inode);

	return ret;
}

static const struct file_operations zonefs_file_operations = {
	.open		= generic_file_open,
	.fsync		= zonefs_file_fsync,
	.mmap		= zonefs_file_mmap,
	.llseek		= zonefs_file_llseek,
	.read_iter	= zonefs_file_read_iter,
	.write_iter	= zonefs_file_write_iter,
	.splice_read	= generic_file_splice_read,
	.splice_write	= iter_file_splice_write,
	.iopoll		= iomap_dio_iopoll,
};

static struct kmem_cache *zonefs_inode_cachep;

static struct inode *zonefs_alloc_inode(struct super_block *sb)
{
	struct zonefs_inode_info *zi;

	zi = kmem_cache_alloc(zonefs_inode_cachep, GFP_KERNEL);
	if (!zi)
		return NULL;

	inode_init_once(&zi->i_vnode);
	mutex_init(&zi->i_truncate_mutex);
	init_rwsem(&zi->i_mmap_sem);

	return &zi->i_vnode;
}

static void zonefs_free_inode(struct inode *inode)
{
	kmem_cache_free(zonefs_inode_cachep, ZONEFS_I(inode));
}

/*
 * File system stat.
 */
static int zonefs_statfs(struct dentry *dentry, struct kstatfs *buf)
{
	struct super_block *sb = dentry->d_sb;
	struct zonefs_sb_info *sbi = ZONEFS_SB(sb);
	enum zonefs_ztype t;
	u64 fsid;

	buf->f_type = ZONEFS_MAGIC;
	buf->f_bsize = sb->s_blocksize;
	buf->f_namelen = ZONEFS_NAME_MAX;

	spin_lock(&sbi->s_lock);

	buf->f_blocks = sbi->s_blocks;
	if (WARN_ON(sbi->s_used_blocks > sbi->s_blocks))
		buf->f_bfree = 0;
	else
		buf->f_bfree = buf->f_blocks - sbi->s_used_blocks;
	buf->f_bavail = buf->f_bfree;

	for (t = 0; t < ZONEFS_ZTYPE_MAX; t++) {
		if (sbi->s_nr_files[t])
			buf->f_files += sbi->s_nr_files[t] + 1;
	}
	buf->f_ffree = 0;

	spin_unlock(&sbi->s_lock);

	fsid = le64_to_cpup((void *)sbi->s_uuid.b) ^
		le64_to_cpup((void *)sbi->s_uuid.b + sizeof(u64));
	buf->f_fsid.val[0] = (u32)fsid;
	buf->f_fsid.val[1] = (u32)(fsid >> 32);

	return 0;
}

enum {
	Opt_errors_ro, Opt_errors_zro, Opt_errors_zol, Opt_errors_repair,
	Opt_err,
};

static const match_table_t tokens = {
	{ Opt_errors_ro,	"errors=remount-ro"},
	{ Opt_errors_zro,	"errors=zone-ro"},
	{ Opt_errors_zol,	"errors=zone-offline"},
	{ Opt_errors_repair,	"errors=repair"},
	{ Opt_err,		NULL}
};

static int zonefs_parse_options(struct super_block *sb, char *options)
{
	struct zonefs_sb_info *sbi = ZONEFS_SB(sb);
	substring_t args[MAX_OPT_ARGS];
	char *p;

	if (!options)
		return 0;

	while ((p = strsep(&options, ",")) != NULL) {
		int token;

		if (!*p)
			continue;

		token = match_token(p, tokens, args);
		switch (token) {
		case Opt_errors_ro:
			sbi->s_mount_opts &= ~ZONEFS_MNTOPT_ERRORS_MASK;
			sbi->s_mount_opts |= ZONEFS_MNTOPT_ERRORS_RO;
			break;
		case Opt_errors_zro:
			sbi->s_mount_opts &= ~ZONEFS_MNTOPT_ERRORS_MASK;
			sbi->s_mount_opts |= ZONEFS_MNTOPT_ERRORS_ZRO;
			break;
		case Opt_errors_zol:
			sbi->s_mount_opts &= ~ZONEFS_MNTOPT_ERRORS_MASK;
			sbi->s_mount_opts |= ZONEFS_MNTOPT_ERRORS_ZOL;
			break;
		case Opt_errors_repair:
			sbi->s_mount_opts &= ~ZONEFS_MNTOPT_ERRORS_MASK;
			sbi->s_mount_opts |= ZONEFS_MNTOPT_ERRORS_REPAIR;
			break;
		default:
			return -EINVAL;
		}
	}

	return 0;
}

static int zonefs_show_options(struct seq_file *seq, struct dentry *root)
{
	struct zonefs_sb_info *sbi = ZONEFS_SB(root->d_sb);

	if (sbi->s_mount_opts & ZONEFS_MNTOPT_ERRORS_RO)
		seq_puts(seq, ",errors=remount-ro");
	if (sbi->s_mount_opts & ZONEFS_MNTOPT_ERRORS_ZRO)
		seq_puts(seq, ",errors=zone-ro");
	if (sbi->s_mount_opts & ZONEFS_MNTOPT_ERRORS_ZOL)
		seq_puts(seq, ",errors=zone-offline");
	if (sbi->s_mount_opts & ZONEFS_MNTOPT_ERRORS_REPAIR)
		seq_puts(seq, ",errors=repair");

	return 0;
}

static int zonefs_remount(struct super_block *sb, int *flags, char *data)
{
	sync_filesystem(sb);

	return zonefs_parse_options(sb, data);
}

static const struct super_operations zonefs_sops = {
	.alloc_inode	= zonefs_alloc_inode,
	.free_inode	= zonefs_free_inode,
	.statfs		= zonefs_statfs,
	.remount_fs	= zonefs_remount,
	.show_options	= zonefs_show_options,
};

static const struct inode_operations zonefs_dir_inode_operations = {
	.lookup		= simple_lookup,
	.setattr	= zonefs_inode_setattr,
};

static void zonefs_init_dir_inode(struct inode *parent, struct inode *inode,
				  enum zonefs_ztype type)
{
	struct super_block *sb = parent->i_sb;

	inode->i_ino = blkdev_nr_zones(sb->s_bdev->bd_disk) + type + 1;
	inode_init_owner(inode, parent, S_IFDIR | 0555);
	inode->i_op = &zonefs_dir_inode_operations;
	inode->i_fop = &simple_dir_operations;
	set_nlink(inode, 2);
	inc_nlink(parent);
}

static void zonefs_init_file_inode(struct inode *inode, struct blk_zone *zone,
				   enum zonefs_ztype type)
{
	struct super_block *sb = inode->i_sb;
	struct zonefs_sb_info *sbi = ZONEFS_SB(sb);
	struct zonefs_inode_info *zi = ZONEFS_I(inode);

	inode->i_ino = zone->start >> sbi->s_zone_sectors_shift;
	inode->i_mode = S_IFREG | sbi->s_perm;

	zi->i_ztype = type;
	zi->i_zsector = zone->start;
	zi->i_max_size = min_t(loff_t, MAX_LFS_FILESIZE,
			       zone->len << SECTOR_SHIFT);
	zi->i_wpoffset = zonefs_check_zone_condition(inode, zone, true, true);

	inode->i_uid = sbi->s_uid;
	inode->i_gid = sbi->s_gid;
	inode->i_size = zi->i_wpoffset;
	inode->i_blocks = zone->len;

	inode->i_op = &zonefs_file_inode_operations;
	inode->i_fop = &zonefs_file_operations;
	inode->i_mapping->a_ops = &zonefs_file_aops;

	sb->s_maxbytes = max(zi->i_max_size, sb->s_maxbytes);
	sbi->s_blocks += zi->i_max_size >> sb->s_blocksize_bits;
	sbi->s_used_blocks += zi->i_wpoffset >> sb->s_blocksize_bits;
}

static struct dentry *zonefs_create_inode(struct dentry *parent,
					const char *name, struct blk_zone *zone,
					enum zonefs_ztype type)
{
	struct inode *dir = d_inode(parent);
	struct dentry *dentry;
	struct inode *inode;

	dentry = d_alloc_name(parent, name);
	if (!dentry)
		return NULL;

	inode = new_inode(parent->d_sb);
	if (!inode)
		goto dput;

	inode->i_ctime = inode->i_mtime = inode->i_atime = dir->i_ctime;
	if (zone)
		zonefs_init_file_inode(inode, zone, type);
	else
		zonefs_init_dir_inode(dir, inode, type);
	d_add(dentry, inode);
	dir->i_size++;

	return dentry;

dput:
	dput(dentry);

	return NULL;
}

struct zonefs_zone_data {
	struct super_block	*sb;
	unsigned int		nr_zones[ZONEFS_ZTYPE_MAX];
	struct blk_zone		*zones;
};

/*
 * Create a zone group and populate it with zone files.
 */
static int zonefs_create_zgroup(struct zonefs_zone_data *zd,
				enum zonefs_ztype type)
{
	struct super_block *sb = zd->sb;
	struct zonefs_sb_info *sbi = ZONEFS_SB(sb);
	struct blk_zone *zone, *next, *end;
	const char *zgroup_name;
	char *file_name;
	struct dentry *dir;
	unsigned int n = 0;
	int ret;

	/* If the group is empty, there is nothing to do */
	if (!zd->nr_zones[type])
		return 0;

	file_name = kmalloc(ZONEFS_NAME_MAX, GFP_KERNEL);
	if (!file_name)
		return -ENOMEM;

	if (type == ZONEFS_ZTYPE_CNV)
		zgroup_name = "cnv";
	else
		zgroup_name = "seq";

	dir = zonefs_create_inode(sb->s_root, zgroup_name, NULL, type);
	if (!dir) {
		ret = -ENOMEM;
		goto free;
	}

	/*
	 * The first zone contains the super block: skip it.
	 */
	end = zd->zones + blkdev_nr_zones(sb->s_bdev->bd_disk);
	for (zone = &zd->zones[1]; zone < end; zone = next) {

		next = zone + 1;
		if (zonefs_zone_type(zone) != type)
			continue;

		/*
		 * For conventional zones, contiguous zones can be aggregated
		 * together to form larger files. Note that this overwrites the
		 * length of the first zone of the set of contiguous zones
		 * aggregated together. If one offline or read-only zone is
		 * found, assume that all zones aggregated have the same
		 * condition.
		 */
		if (type == ZONEFS_ZTYPE_CNV &&
		    (sbi->s_features & ZONEFS_F_AGGRCNV)) {
			for (; next < end; next++) {
				if (zonefs_zone_type(next) != type)
					break;
				zone->len += next->len;
				if (next->cond == BLK_ZONE_COND_READONLY &&
				    zone->cond != BLK_ZONE_COND_OFFLINE)
					zone->cond = BLK_ZONE_COND_READONLY;
				else if (next->cond == BLK_ZONE_COND_OFFLINE)
					zone->cond = BLK_ZONE_COND_OFFLINE;
			}
		}

		/*
		 * Use the file number within its group as file name.
		 */
		snprintf(file_name, ZONEFS_NAME_MAX - 1, "%u", n);
		if (!zonefs_create_inode(dir, file_name, zone, type)) {
			ret = -ENOMEM;
			goto free;
		}

		n++;
	}

	zonefs_info(sb, "Zone group \"%s\" has %u file%s\n",
		    zgroup_name, n, n > 1 ? "s" : "");

	sbi->s_nr_files[type] = n;
	ret = 0;

free:
	kfree(file_name);

	return ret;
}

static int zonefs_get_zone_info_cb(struct blk_zone *zone, unsigned int idx,
				   void *data)
{
	struct zonefs_zone_data *zd = data;

	/*
	 * Count the number of usable zones: the first zone at index 0 contains
	 * the super block and is ignored.
	 */
	switch (zone->type) {
	case BLK_ZONE_TYPE_CONVENTIONAL:
		zone->wp = zone->start + zone->len;
		if (idx)
			zd->nr_zones[ZONEFS_ZTYPE_CNV]++;
		break;
	case BLK_ZONE_TYPE_SEQWRITE_REQ:
	case BLK_ZONE_TYPE_SEQWRITE_PREF:
		if (idx)
			zd->nr_zones[ZONEFS_ZTYPE_SEQ]++;
		break;
	default:
		zonefs_err(zd->sb, "Unsupported zone type 0x%x\n",
			   zone->type);
		return -EIO;
	}

	memcpy(&zd->zones[idx], zone, sizeof(struct blk_zone));

	return 0;
}

static int zonefs_get_zone_info(struct zonefs_zone_data *zd)
{
	struct block_device *bdev = zd->sb->s_bdev;
	int ret;

	zd->zones = kvcalloc(blkdev_nr_zones(bdev->bd_disk),
			     sizeof(struct blk_zone), GFP_KERNEL);
	if (!zd->zones)
		return -ENOMEM;

	/* Get zones information from the device */
	ret = blkdev_report_zones(bdev, 0, BLK_ALL_ZONES,
				  zonefs_get_zone_info_cb, zd);
	if (ret < 0) {
		zonefs_err(zd->sb, "Zone report failed %d\n", ret);
		return ret;
	}

	if (ret != blkdev_nr_zones(bdev->bd_disk)) {
		zonefs_err(zd->sb, "Invalid zone report (%d/%u zones)\n",
			   ret, blkdev_nr_zones(bdev->bd_disk));
		return -EIO;
	}

	return 0;
}

static inline void zonefs_cleanup_zone_info(struct zonefs_zone_data *zd)
{
	kvfree(zd->zones);
}

/*
 * Read super block information from the device.
 */
static int zonefs_read_super(struct super_block *sb)
{
	struct zonefs_sb_info *sbi = ZONEFS_SB(sb);
	struct zonefs_super *super;
	u32 crc, stored_crc;
	struct page *page;
	struct bio_vec bio_vec;
	struct bio bio;
	int ret;

	page = alloc_page(GFP_KERNEL);
	if (!page)
		return -ENOMEM;

	bio_init(&bio, &bio_vec, 1);
	bio.bi_iter.bi_sector = 0;
	bio.bi_opf = REQ_OP_READ;
	bio_set_dev(&bio, sb->s_bdev);
	bio_add_page(&bio, page, PAGE_SIZE, 0);

	ret = submit_bio_wait(&bio);
	if (ret)
		goto free_page;

	super = kmap(page);

	ret = -EINVAL;
	if (le32_to_cpu(super->s_magic) != ZONEFS_MAGIC)
		goto unmap;

	stored_crc = le32_to_cpu(super->s_crc);
	super->s_crc = 0;
	crc = crc32(~0U, (unsigned char *)super, sizeof(struct zonefs_super));
	if (crc != stored_crc) {
		zonefs_err(sb, "Invalid checksum (Expected 0x%08x, got 0x%08x)",
			   crc, stored_crc);
		goto unmap;
	}

	sbi->s_features = le64_to_cpu(super->s_features);
	if (sbi->s_features & ~ZONEFS_F_DEFINED_FEATURES) {
		zonefs_err(sb, "Unknown features set 0x%llx\n",
			   sbi->s_features);
		goto unmap;
	}

	if (sbi->s_features & ZONEFS_F_UID) {
		sbi->s_uid = make_kuid(current_user_ns(),
				       le32_to_cpu(super->s_uid));
		if (!uid_valid(sbi->s_uid)) {
			zonefs_err(sb, "Invalid UID feature\n");
			goto unmap;
		}
	}

	if (sbi->s_features & ZONEFS_F_GID) {
		sbi->s_gid = make_kgid(current_user_ns(),
				       le32_to_cpu(super->s_gid));
		if (!gid_valid(sbi->s_gid)) {
			zonefs_err(sb, "Invalid GID feature\n");
			goto unmap;
		}
	}

	if (sbi->s_features & ZONEFS_F_PERM)
		sbi->s_perm = le32_to_cpu(super->s_perm);

	if (memchr_inv(super->s_reserved, 0, sizeof(super->s_reserved))) {
		zonefs_err(sb, "Reserved area is being used\n");
		goto unmap;
	}

	import_uuid(&sbi->s_uuid, super->s_uuid);
	ret = 0;

unmap:
	kunmap(page);
free_page:
	__free_page(page);

	return ret;
}

/*
 * Check that the device is zoned. If it is, get the list of zones and create
 * sub-directories and files according to the device zone configuration and
 * format options.
 */
static int zonefs_fill_super(struct super_block *sb, void *data, int silent)
{
	struct zonefs_zone_data zd;
	struct zonefs_sb_info *sbi;
	struct inode *inode;
	enum zonefs_ztype t;
	int ret;

	if (!bdev_is_zoned(sb->s_bdev)) {
		zonefs_err(sb, "Not a zoned block device\n");
		return -EINVAL;
	}

	/*
	 * Initialize super block information: the maximum file size is updated
	 * when the zone files are created so that the format option
	 * ZONEFS_F_AGGRCNV which increases the maximum file size of a file
	 * beyond the zone size is taken into account.
	 */
	sbi = kzalloc(sizeof(*sbi), GFP_KERNEL);
	if (!sbi)
		return -ENOMEM;

	spin_lock_init(&sbi->s_lock);
	sb->s_fs_info = sbi;
	sb->s_magic = ZONEFS_MAGIC;
	sb->s_maxbytes = 0;
	sb->s_op = &zonefs_sops;
	sb->s_time_gran	= 1;

	/*
	 * The block size is set to the device physical sector size to ensure
	 * that write operations on 512e devices (512B logical block and 4KB
	 * physical block) are always aligned to the device physical blocks,
	 * as mandated by the ZBC/ZAC specifications.
	 */
	sb_set_blocksize(sb, bdev_physical_block_size(sb->s_bdev));
	sbi->s_zone_sectors_shift = ilog2(bdev_zone_sectors(sb->s_bdev));
	sbi->s_uid = GLOBAL_ROOT_UID;
	sbi->s_gid = GLOBAL_ROOT_GID;
	sbi->s_perm = 0640;
	sbi->s_mount_opts = ZONEFS_MNTOPT_ERRORS_RO;

	ret = zonefs_read_super(sb);
	if (ret)
		return ret;

	ret = zonefs_parse_options(sb, data);
	if (ret)
		return ret;

	memset(&zd, 0, sizeof(struct zonefs_zone_data));
	zd.sb = sb;
	ret = zonefs_get_zone_info(&zd);
	if (ret)
		goto cleanup;

	zonefs_info(sb, "Mounting %u zones",
		    blkdev_nr_zones(sb->s_bdev->bd_disk));

	/* Create root directory inode */
	ret = -ENOMEM;
	inode = new_inode(sb);
	if (!inode)
		goto cleanup;

	inode->i_ino = blkdev_nr_zones(sb->s_bdev->bd_disk);
	inode->i_mode = S_IFDIR | 0555;
	inode->i_ctime = inode->i_mtime = inode->i_atime = current_time(inode);
	inode->i_op = &zonefs_dir_inode_operations;
	inode->i_fop = &simple_dir_operations;
	set_nlink(inode, 2);

	sb->s_root = d_make_root(inode);
	if (!sb->s_root)
		goto cleanup;

	/* Create and populate files in zone groups directories */
	for (t = 0; t < ZONEFS_ZTYPE_MAX; t++) {
		ret = zonefs_create_zgroup(&zd, t);
		if (ret)
			break;
	}

cleanup:
	zonefs_cleanup_zone_info(&zd);

	return ret;
}

static struct dentry *zonefs_mount(struct file_system_type *fs_type,
				   int flags, const char *dev_name, void *data)
{
	return mount_bdev(fs_type, flags, dev_name, data, zonefs_fill_super);
}

static void zonefs_kill_super(struct super_block *sb)
{
	struct zonefs_sb_info *sbi = ZONEFS_SB(sb);

	if (sb->s_root)
		d_genocide(sb->s_root);
	kill_block_super(sb);
	kfree(sbi);
}

/*
 * File system definition and registration.
 */
static struct file_system_type zonefs_type = {
	.owner		= THIS_MODULE,
	.name		= "zonefs",
	.mount		= zonefs_mount,
	.kill_sb	= zonefs_kill_super,
	.fs_flags	= FS_REQUIRES_DEV,
};

static int __init zonefs_init_inodecache(void)
{
	zonefs_inode_cachep = kmem_cache_create("zonefs_inode_cache",
			sizeof(struct zonefs_inode_info), 0,
			(SLAB_RECLAIM_ACCOUNT | SLAB_MEM_SPREAD | SLAB_ACCOUNT),
			NULL);
	if (zonefs_inode_cachep == NULL)
		return -ENOMEM;
	return 0;
}

static void zonefs_destroy_inodecache(void)
{
	/*
	 * Make sure all delayed rcu free inodes are flushed before we
	 * destroy the inode cache.
	 */
	rcu_barrier();
	kmem_cache_destroy(zonefs_inode_cachep);
}

static int __init zonefs_init(void)
{
	int ret;

	BUILD_BUG_ON(sizeof(struct zonefs_super) != ZONEFS_SUPER_SIZE);

	ret = zonefs_init_inodecache();
	if (ret)
		return ret;

	ret = register_filesystem(&zonefs_type);
	if (ret) {
		zonefs_destroy_inodecache();
		return ret;
	}

	return 0;
}

static void __exit zonefs_exit(void)
{
	zonefs_destroy_inodecache();
	unregister_filesystem(&zonefs_type);
}

MODULE_AUTHOR("Damien Le Moal");
MODULE_DESCRIPTION("Zone file system for zoned block devices");
MODULE_LICENSE("GPL");
module_init(zonefs_init);
module_exit(zonefs_exit);
