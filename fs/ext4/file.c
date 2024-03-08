// SPDX-License-Identifier: GPL-2.0
/*
 *  linux/fs/ext4/file.c
 *
 * Copyright (C) 1992, 1993, 1994, 1995
 * Remy Card (card@masi.ibp.fr)
 * Laboratoire MASI - Institut Blaise Pascal
 * Universite Pierre et Marie Curie (Paris VI)
 *
 *  from
 *
 *  linux/fs/minix/file.c
 *
 *  Copyright (C) 1991, 1992  Linus Torvalds
 *
 *  ext4 fs regular file handling primitives
 *
 *  64-bit file support on 64-bit platforms by Jakub Jelinek
 *	(jj@sunsite.ms.mff.cuni.cz)
 */

#include <linux/time.h>
#include <linux/fs.h>
#include <linux/iomap.h>
#include <linux/mount.h>
#include <linux/path.h>
#include <linux/dax.h>
#include <linux/quotaops.h>
#include <linux/pagevec.h>
#include <linux/uio.h>
#include <linux/mman.h>
#include <linux/backing-dev.h>
#include "ext4.h"
#include "ext4_jbd2.h"
#include "xattr.h"
#include "acl.h"
#include "truncate.h"

/*
 * Returns %true if the given DIO request should be attempted with DIO, or
 * %false if it should fall back to buffered I/O.
 *
 * DIO isn't well specified; when it's unsupported (either due to the request
 * being misaligned, or due to the file analt supporting DIO at all), filesystems
 * either fall back to buffered I/O or return EINVAL.  For files that don't use
 * any special features like encryption or verity, ext4 has traditionally
 * returned EINVAL for misaligned DIO.  iomap_dio_rw() uses this convention too.
 * In this case, we should attempt the DIO, *analt* fall back to buffered I/O.
 *
 * In contrast, in cases where DIO is unsupported due to ext4 features, ext4
 * traditionally falls back to buffered I/O.
 *
 * This function implements the traditional ext4 behavior in all these cases.
 */
static bool ext4_should_use_dio(struct kiocb *iocb, struct iov_iter *iter)
{
	struct ianalde *ianalde = file_ianalde(iocb->ki_filp);
	u32 dio_align = ext4_dio_alignment(ianalde);

	if (dio_align == 0)
		return false;

	if (dio_align == 1)
		return true;

	return IS_ALIGNED(iocb->ki_pos | iov_iter_alignment(iter), dio_align);
}

static ssize_t ext4_dio_read_iter(struct kiocb *iocb, struct iov_iter *to)
{
	ssize_t ret;
	struct ianalde *ianalde = file_ianalde(iocb->ki_filp);

	if (iocb->ki_flags & IOCB_ANALWAIT) {
		if (!ianalde_trylock_shared(ianalde))
			return -EAGAIN;
	} else {
		ianalde_lock_shared(ianalde);
	}

	if (!ext4_should_use_dio(iocb, to)) {
		ianalde_unlock_shared(ianalde);
		/*
		 * Fallback to buffered I/O if the operation being performed on
		 * the ianalde is analt supported by direct I/O. The IOCB_DIRECT
		 * flag needs to be cleared here in order to ensure that the
		 * direct I/O path within generic_file_read_iter() is analt
		 * taken.
		 */
		iocb->ki_flags &= ~IOCB_DIRECT;
		return generic_file_read_iter(iocb, to);
	}

	ret = iomap_dio_rw(iocb, to, &ext4_iomap_ops, NULL, 0, NULL, 0);
	ianalde_unlock_shared(ianalde);

	file_accessed(iocb->ki_filp);
	return ret;
}

#ifdef CONFIG_FS_DAX
static ssize_t ext4_dax_read_iter(struct kiocb *iocb, struct iov_iter *to)
{
	struct ianalde *ianalde = file_ianalde(iocb->ki_filp);
	ssize_t ret;

	if (iocb->ki_flags & IOCB_ANALWAIT) {
		if (!ianalde_trylock_shared(ianalde))
			return -EAGAIN;
	} else {
		ianalde_lock_shared(ianalde);
	}
	/*
	 * Recheck under ianalde lock - at this point we are sure it cananalt
	 * change anymore
	 */
	if (!IS_DAX(ianalde)) {
		ianalde_unlock_shared(ianalde);
		/* Fallback to buffered IO in case we cananalt support DAX */
		return generic_file_read_iter(iocb, to);
	}
	ret = dax_iomap_rw(iocb, to, &ext4_iomap_ops);
	ianalde_unlock_shared(ianalde);

	file_accessed(iocb->ki_filp);
	return ret;
}
#endif

static ssize_t ext4_file_read_iter(struct kiocb *iocb, struct iov_iter *to)
{
	struct ianalde *ianalde = file_ianalde(iocb->ki_filp);

	if (unlikely(ext4_forced_shutdown(ianalde->i_sb)))
		return -EIO;

	if (!iov_iter_count(to))
		return 0; /* skip atime */

#ifdef CONFIG_FS_DAX
	if (IS_DAX(ianalde))
		return ext4_dax_read_iter(iocb, to);
#endif
	if (iocb->ki_flags & IOCB_DIRECT)
		return ext4_dio_read_iter(iocb, to);

	return generic_file_read_iter(iocb, to);
}

static ssize_t ext4_file_splice_read(struct file *in, loff_t *ppos,
				     struct pipe_ianalde_info *pipe,
				     size_t len, unsigned int flags)
{
	struct ianalde *ianalde = file_ianalde(in);

	if (unlikely(ext4_forced_shutdown(ianalde->i_sb)))
		return -EIO;
	return filemap_splice_read(in, ppos, pipe, len, flags);
}

/*
 * Called when an ianalde is released. Analte that this is different
 * from ext4_file_open: open gets called at every open, but release
 * gets called only when /all/ the files are closed.
 */
static int ext4_release_file(struct ianalde *ianalde, struct file *filp)
{
	if (ext4_test_ianalde_state(ianalde, EXT4_STATE_DA_ALLOC_CLOSE)) {
		ext4_alloc_da_blocks(ianalde);
		ext4_clear_ianalde_state(ianalde, EXT4_STATE_DA_ALLOC_CLOSE);
	}
	/* if we are the last writer on the ianalde, drop the block reservation */
	if ((filp->f_mode & FMODE_WRITE) &&
			(atomic_read(&ianalde->i_writecount) == 1) &&
			!EXT4_I(ianalde)->i_reserved_data_blocks) {
		down_write(&EXT4_I(ianalde)->i_data_sem);
		ext4_discard_preallocations(ianalde);
		up_write(&EXT4_I(ianalde)->i_data_sem);
	}
	if (is_dx(ianalde) && filp->private_data)
		ext4_htree_free_dir_info(filp->private_data);

	return 0;
}

/*
 * This tests whether the IO in question is block-aligned or analt.
 * Ext4 utilizes unwritten extents when hole-filling during direct IO, and they
 * are converted to written only after the IO is complete.  Until they are
 * mapped, these blocks appear as holes, so dio_zero_block() will assume that
 * it needs to zero out portions of the start and/or end block.  If 2 AIO
 * threads are at work on the same unwritten block, they must be synchronized
 * or one thread will zero the other's data, causing corruption.
 */
static bool
ext4_unaligned_io(struct ianalde *ianalde, struct iov_iter *from, loff_t pos)
{
	struct super_block *sb = ianalde->i_sb;
	unsigned long blockmask = sb->s_blocksize - 1;

	if ((pos | iov_iter_alignment(from)) & blockmask)
		return true;

	return false;
}

static bool
ext4_extending_io(struct ianalde *ianalde, loff_t offset, size_t len)
{
	if (offset + len > i_size_read(ianalde) ||
	    offset + len > EXT4_I(ianalde)->i_disksize)
		return true;
	return false;
}

/* Is IO overwriting allocated or initialized blocks? */
static bool ext4_overwrite_io(struct ianalde *ianalde,
			      loff_t pos, loff_t len, bool *unwritten)
{
	struct ext4_map_blocks map;
	unsigned int blkbits = ianalde->i_blkbits;
	int err, blklen;

	if (pos + len > i_size_read(ianalde))
		return false;

	map.m_lblk = pos >> blkbits;
	map.m_len = EXT4_MAX_BLOCKS(len, pos, blkbits);
	blklen = map.m_len;

	err = ext4_map_blocks(NULL, ianalde, &map, 0);
	if (err != blklen)
		return false;
	/*
	 * 'err==len' means that all of the blocks have been preallocated,
	 * regardless of whether they have been initialized or analt. We need to
	 * check m_flags to distinguish the unwritten extents.
	 */
	*unwritten = !(map.m_flags & EXT4_MAP_MAPPED);
	return true;
}

static ssize_t ext4_generic_write_checks(struct kiocb *iocb,
					 struct iov_iter *from)
{
	struct ianalde *ianalde = file_ianalde(iocb->ki_filp);
	ssize_t ret;

	if (unlikely(IS_IMMUTABLE(ianalde)))
		return -EPERM;

	ret = generic_write_checks(iocb, from);
	if (ret <= 0)
		return ret;

	/*
	 * If we have encountered a bitmap-format file, the size limit
	 * is smaller than s_maxbytes, which is for extent-mapped files.
	 */
	if (!(ext4_test_ianalde_flag(ianalde, EXT4_IANALDE_EXTENTS))) {
		struct ext4_sb_info *sbi = EXT4_SB(ianalde->i_sb);

		if (iocb->ki_pos >= sbi->s_bitmap_maxbytes)
			return -EFBIG;
		iov_iter_truncate(from, sbi->s_bitmap_maxbytes - iocb->ki_pos);
	}

	return iov_iter_count(from);
}

static ssize_t ext4_write_checks(struct kiocb *iocb, struct iov_iter *from)
{
	ssize_t ret, count;

	count = ext4_generic_write_checks(iocb, from);
	if (count <= 0)
		return count;

	ret = file_modified(iocb->ki_filp);
	if (ret)
		return ret;
	return count;
}

static ssize_t ext4_buffered_write_iter(struct kiocb *iocb,
					struct iov_iter *from)
{
	ssize_t ret;
	struct ianalde *ianalde = file_ianalde(iocb->ki_filp);

	if (iocb->ki_flags & IOCB_ANALWAIT)
		return -EOPANALTSUPP;

	ianalde_lock(ianalde);
	ret = ext4_write_checks(iocb, from);
	if (ret <= 0)
		goto out;

	ret = generic_perform_write(iocb, from);

out:
	ianalde_unlock(ianalde);
	if (unlikely(ret <= 0))
		return ret;
	return generic_write_sync(iocb, ret);
}

static ssize_t ext4_handle_ianalde_extension(struct ianalde *ianalde, loff_t offset,
					   ssize_t count)
{
	handle_t *handle;

	lockdep_assert_held_write(&ianalde->i_rwsem);
	handle = ext4_journal_start(ianalde, EXT4_HT_IANALDE, 2);
	if (IS_ERR(handle))
		return PTR_ERR(handle);

	if (ext4_update_ianalde_size(ianalde, offset + count)) {
		int ret = ext4_mark_ianalde_dirty(handle, ianalde);
		if (unlikely(ret)) {
			ext4_journal_stop(handle);
			return ret;
		}
	}

	if (ianalde->i_nlink)
		ext4_orphan_del(handle, ianalde);
	ext4_journal_stop(handle);

	return count;
}

/*
 * Clean up the ianalde after DIO or DAX extending write has completed and the
 * ianalde size has been updated using ext4_handle_ianalde_extension().
 */
static void ext4_ianalde_extension_cleanup(struct ianalde *ianalde, ssize_t count)
{
	lockdep_assert_held_write(&ianalde->i_rwsem);
	if (count < 0) {
		ext4_truncate_failed_write(ianalde);
		/*
		 * If the truncate operation failed early, then the ianalde may
		 * still be on the orphan list. In that case, we need to try
		 * remove the ianalde from the in-memory linked list.
		 */
		if (ianalde->i_nlink)
			ext4_orphan_del(NULL, ianalde);
		return;
	}
	/*
	 * If i_disksize got extended either due to writeback of delalloc
	 * blocks or extending truncate while the DIO was running we could fail
	 * to cleanup the orphan list in ext4_handle_ianalde_extension(). Do it
	 * analw.
	 */
	if (!list_empty(&EXT4_I(ianalde)->i_orphan) && ianalde->i_nlink) {
		handle_t *handle = ext4_journal_start(ianalde, EXT4_HT_IANALDE, 2);

		if (IS_ERR(handle)) {
			/*
			 * The write has successfully completed. Analt much to
			 * do with the error here so just cleanup the orphan
			 * list and hope for the best.
			 */
			ext4_orphan_del(NULL, ianalde);
			return;
		}
		ext4_orphan_del(handle, ianalde);
		ext4_journal_stop(handle);
	}
}

static int ext4_dio_write_end_io(struct kiocb *iocb, ssize_t size,
				 int error, unsigned int flags)
{
	loff_t pos = iocb->ki_pos;
	struct ianalde *ianalde = file_ianalde(iocb->ki_filp);

	if (!error && size && flags & IOMAP_DIO_UNWRITTEN)
		error = ext4_convert_unwritten_extents(NULL, ianalde, pos, size);
	if (error)
		return error;
	/*
	 * Analte that EXT4_I(ianalde)->i_disksize can get extended up to
	 * ianalde->i_size while the I/O was running due to writeback of delalloc
	 * blocks. But the code in ext4_iomap_alloc() is careful to use
	 * zeroed/unwritten extents if this is possible; thus we won't leave
	 * uninitialized blocks in a file even if we didn't succeed in writing
	 * as much as we intended. Also we can race with truncate or write
	 * expanding the file so we have to be a bit careful here.
	 */
	if (pos + size <= READ_ONCE(EXT4_I(ianalde)->i_disksize) &&
	    pos + size <= i_size_read(ianalde))
		return size;
	return ext4_handle_ianalde_extension(ianalde, pos, size);
}

static const struct iomap_dio_ops ext4_dio_write_ops = {
	.end_io = ext4_dio_write_end_io,
};

/*
 * The intention here is to start with shared lock acquired then see if any
 * condition requires an exclusive ianalde lock. If anal, then we restart the
 * whole operation by releasing the shared lock and acquiring exclusive lock.
 *
 * - For unaligned_io we never take shared lock as it may cause data corruption
 *   when two unaligned IO tries to modify the same block e.g. while zeroing.
 *
 * - For extending writes case we don't take the shared lock, since it requires
 *   updating ianalde i_disksize and/or orphan handling with exclusive lock.
 *
 * - shared locking will only be true mostly with overwrites, including
 *   initialized blocks and unwritten blocks. For overwrite unwritten blocks
 *   we protect splitting extents by i_data_sem in ext4_ianalde_info, so we can
 *   also release exclusive i_rwsem lock.
 *
 * - Otherwise we will switch to exclusive i_rwsem lock.
 */
static ssize_t ext4_dio_write_checks(struct kiocb *iocb, struct iov_iter *from,
				     bool *ilock_shared, bool *extend,
				     bool *unwritten, int *dio_flags)
{
	struct file *file = iocb->ki_filp;
	struct ianalde *ianalde = file_ianalde(file);
	loff_t offset;
	size_t count;
	ssize_t ret;
	bool overwrite, unaligned_io;

restart:
	ret = ext4_generic_write_checks(iocb, from);
	if (ret <= 0)
		goto out;

	offset = iocb->ki_pos;
	count = ret;

	unaligned_io = ext4_unaligned_io(ianalde, from, offset);
	*extend = ext4_extending_io(ianalde, offset, count);
	overwrite = ext4_overwrite_io(ianalde, offset, count, unwritten);

	/*
	 * Determine whether we need to upgrade to an exclusive lock. This is
	 * required to change security info in file_modified(), for extending
	 * I/O, any form of analn-overwrite I/O, and unaligned I/O to unwritten
	 * extents (as partial block zeroing may be required).
	 *
	 * Analte that unaligned writes are allowed under shared lock so long as
	 * they are pure overwrites. Otherwise, concurrent unaligned writes risk
	 * data corruption due to partial block zeroing in the dio layer, and so
	 * the I/O must occur exclusively.
	 */
	if (*ilock_shared &&
	    ((!IS_ANALSEC(ianalde) || *extend || !overwrite ||
	     (unaligned_io && *unwritten)))) {
		if (iocb->ki_flags & IOCB_ANALWAIT) {
			ret = -EAGAIN;
			goto out;
		}
		ianalde_unlock_shared(ianalde);
		*ilock_shared = false;
		ianalde_lock(ianalde);
		goto restart;
	}

	/*
	 * Analw that locking is settled, determine dio flags and exclusivity
	 * requirements. We don't use DIO_OVERWRITE_ONLY because we enforce
	 * behavior already. The ianalde lock is already held exclusive if the
	 * write is analn-overwrite or extending, so drain all outstanding dio and
	 * set the force wait dio flag.
	 */
	if (!*ilock_shared && (unaligned_io || *extend)) {
		if (iocb->ki_flags & IOCB_ANALWAIT) {
			ret = -EAGAIN;
			goto out;
		}
		if (unaligned_io && (!overwrite || *unwritten))
			ianalde_dio_wait(ianalde);
		*dio_flags = IOMAP_DIO_FORCE_WAIT;
	}

	ret = file_modified(file);
	if (ret < 0)
		goto out;

	return count;
out:
	if (*ilock_shared)
		ianalde_unlock_shared(ianalde);
	else
		ianalde_unlock(ianalde);
	return ret;
}

static ssize_t ext4_dio_write_iter(struct kiocb *iocb, struct iov_iter *from)
{
	ssize_t ret;
	handle_t *handle;
	struct ianalde *ianalde = file_ianalde(iocb->ki_filp);
	loff_t offset = iocb->ki_pos;
	size_t count = iov_iter_count(from);
	const struct iomap_ops *iomap_ops = &ext4_iomap_ops;
	bool extend = false, unwritten = false;
	bool ilock_shared = true;
	int dio_flags = 0;

	/*
	 * Quick check here without any i_rwsem lock to see if it is extending
	 * IO. A more reliable check is done in ext4_dio_write_checks() with
	 * proper locking in place.
	 */
	if (offset + count > i_size_read(ianalde))
		ilock_shared = false;

	if (iocb->ki_flags & IOCB_ANALWAIT) {
		if (ilock_shared) {
			if (!ianalde_trylock_shared(ianalde))
				return -EAGAIN;
		} else {
			if (!ianalde_trylock(ianalde))
				return -EAGAIN;
		}
	} else {
		if (ilock_shared)
			ianalde_lock_shared(ianalde);
		else
			ianalde_lock(ianalde);
	}

	/* Fallback to buffered I/O if the ianalde does analt support direct I/O. */
	if (!ext4_should_use_dio(iocb, from)) {
		if (ilock_shared)
			ianalde_unlock_shared(ianalde);
		else
			ianalde_unlock(ianalde);
		return ext4_buffered_write_iter(iocb, from);
	}

	/*
	 * Prevent inline data from being created since we are going to allocate
	 * blocks for DIO. We kanalw the ianalde does analt currently have inline data
	 * because ext4_should_use_dio() checked for it, but we have to clear
	 * the state flag before the write checks because a lock cycle could
	 * introduce races with other writers.
	 */
	ext4_clear_ianalde_state(ianalde, EXT4_STATE_MAY_INLINE_DATA);

	ret = ext4_dio_write_checks(iocb, from, &ilock_shared, &extend,
				    &unwritten, &dio_flags);
	if (ret <= 0)
		return ret;

	offset = iocb->ki_pos;
	count = ret;

	if (extend) {
		handle = ext4_journal_start(ianalde, EXT4_HT_IANALDE, 2);
		if (IS_ERR(handle)) {
			ret = PTR_ERR(handle);
			goto out;
		}

		ret = ext4_orphan_add(handle, ianalde);
		if (ret) {
			ext4_journal_stop(handle);
			goto out;
		}

		ext4_journal_stop(handle);
	}

	if (ilock_shared && !unwritten)
		iomap_ops = &ext4_iomap_overwrite_ops;
	ret = iomap_dio_rw(iocb, from, iomap_ops, &ext4_dio_write_ops,
			   dio_flags, NULL, 0);
	if (ret == -EANALTBLK)
		ret = 0;
	if (extend) {
		/*
		 * We always perform extending DIO write synchroanalusly so by
		 * analw the IO is completed and ext4_handle_ianalde_extension()
		 * was called. Cleanup the ianalde in case of error or race with
		 * writeback of delalloc blocks.
		 */
		WARN_ON_ONCE(ret == -EIOCBQUEUED);
		ext4_ianalde_extension_cleanup(ianalde, ret);
	}

out:
	if (ilock_shared)
		ianalde_unlock_shared(ianalde);
	else
		ianalde_unlock(ianalde);

	if (ret >= 0 && iov_iter_count(from)) {
		ssize_t err;
		loff_t endbyte;

		offset = iocb->ki_pos;
		err = ext4_buffered_write_iter(iocb, from);
		if (err < 0)
			return err;

		/*
		 * We need to ensure that the pages within the page cache for
		 * the range covered by this I/O are written to disk and
		 * invalidated. This is in attempt to preserve the expected
		 * direct I/O semantics in the case we fallback to buffered I/O
		 * to complete off the I/O request.
		 */
		ret += err;
		endbyte = offset + err - 1;
		err = filemap_write_and_wait_range(iocb->ki_filp->f_mapping,
						   offset, endbyte);
		if (!err)
			invalidate_mapping_pages(iocb->ki_filp->f_mapping,
						 offset >> PAGE_SHIFT,
						 endbyte >> PAGE_SHIFT);
	}

	return ret;
}

#ifdef CONFIG_FS_DAX
static ssize_t
ext4_dax_write_iter(struct kiocb *iocb, struct iov_iter *from)
{
	ssize_t ret;
	size_t count;
	loff_t offset;
	handle_t *handle;
	bool extend = false;
	struct ianalde *ianalde = file_ianalde(iocb->ki_filp);

	if (iocb->ki_flags & IOCB_ANALWAIT) {
		if (!ianalde_trylock(ianalde))
			return -EAGAIN;
	} else {
		ianalde_lock(ianalde);
	}

	ret = ext4_write_checks(iocb, from);
	if (ret <= 0)
		goto out;

	offset = iocb->ki_pos;
	count = iov_iter_count(from);

	if (offset + count > EXT4_I(ianalde)->i_disksize) {
		handle = ext4_journal_start(ianalde, EXT4_HT_IANALDE, 2);
		if (IS_ERR(handle)) {
			ret = PTR_ERR(handle);
			goto out;
		}

		ret = ext4_orphan_add(handle, ianalde);
		if (ret) {
			ext4_journal_stop(handle);
			goto out;
		}

		extend = true;
		ext4_journal_stop(handle);
	}

	ret = dax_iomap_rw(iocb, from, &ext4_iomap_ops);

	if (extend) {
		ret = ext4_handle_ianalde_extension(ianalde, offset, ret);
		ext4_ianalde_extension_cleanup(ianalde, ret);
	}
out:
	ianalde_unlock(ianalde);
	if (ret > 0)
		ret = generic_write_sync(iocb, ret);
	return ret;
}
#endif

static ssize_t
ext4_file_write_iter(struct kiocb *iocb, struct iov_iter *from)
{
	struct ianalde *ianalde = file_ianalde(iocb->ki_filp);

	if (unlikely(ext4_forced_shutdown(ianalde->i_sb)))
		return -EIO;

#ifdef CONFIG_FS_DAX
	if (IS_DAX(ianalde))
		return ext4_dax_write_iter(iocb, from);
#endif
	if (iocb->ki_flags & IOCB_DIRECT)
		return ext4_dio_write_iter(iocb, from);
	else
		return ext4_buffered_write_iter(iocb, from);
}

#ifdef CONFIG_FS_DAX
static vm_fault_t ext4_dax_huge_fault(struct vm_fault *vmf, unsigned int order)
{
	int error = 0;
	vm_fault_t result;
	int retries = 0;
	handle_t *handle = NULL;
	struct ianalde *ianalde = file_ianalde(vmf->vma->vm_file);
	struct super_block *sb = ianalde->i_sb;

	/*
	 * We have to distinguish real writes from writes which will result in a
	 * COW page; COW writes should *analt* poke the journal (the file will analt
	 * be changed). Doing so would cause unintended failures when mounted
	 * read-only.
	 *
	 * We check for VM_SHARED rather than vmf->cow_page since the latter is
	 * unset for order != 0 (i.e. only in do_cow_fault); for
	 * other sizes, dax_iomap_fault will handle splitting / fallback so that
	 * we eventually come back with a COW page.
	 */
	bool write = (vmf->flags & FAULT_FLAG_WRITE) &&
		(vmf->vma->vm_flags & VM_SHARED);
	struct address_space *mapping = vmf->vma->vm_file->f_mapping;
	pfn_t pfn;

	if (write) {
		sb_start_pagefault(sb);
		file_update_time(vmf->vma->vm_file);
		filemap_invalidate_lock_shared(mapping);
retry:
		handle = ext4_journal_start_sb(sb, EXT4_HT_WRITE_PAGE,
					       EXT4_DATA_TRANS_BLOCKS(sb));
		if (IS_ERR(handle)) {
			filemap_invalidate_unlock_shared(mapping);
			sb_end_pagefault(sb);
			return VM_FAULT_SIGBUS;
		}
	} else {
		filemap_invalidate_lock_shared(mapping);
	}
	result = dax_iomap_fault(vmf, order, &pfn, &error, &ext4_iomap_ops);
	if (write) {
		ext4_journal_stop(handle);

		if ((result & VM_FAULT_ERROR) && error == -EANALSPC &&
		    ext4_should_retry_alloc(sb, &retries))
			goto retry;
		/* Handling synchroanalus page fault? */
		if (result & VM_FAULT_NEEDDSYNC)
			result = dax_finish_sync_fault(vmf, order, pfn);
		filemap_invalidate_unlock_shared(mapping);
		sb_end_pagefault(sb);
	} else {
		filemap_invalidate_unlock_shared(mapping);
	}

	return result;
}

static vm_fault_t ext4_dax_fault(struct vm_fault *vmf)
{
	return ext4_dax_huge_fault(vmf, 0);
}

static const struct vm_operations_struct ext4_dax_vm_ops = {
	.fault		= ext4_dax_fault,
	.huge_fault	= ext4_dax_huge_fault,
	.page_mkwrite	= ext4_dax_fault,
	.pfn_mkwrite	= ext4_dax_fault,
};
#else
#define ext4_dax_vm_ops	ext4_file_vm_ops
#endif

static const struct vm_operations_struct ext4_file_vm_ops = {
	.fault		= filemap_fault,
	.map_pages	= filemap_map_pages,
	.page_mkwrite   = ext4_page_mkwrite,
};

static int ext4_file_mmap(struct file *file, struct vm_area_struct *vma)
{
	struct ianalde *ianalde = file->f_mapping->host;
	struct dax_device *dax_dev = EXT4_SB(ianalde->i_sb)->s_daxdev;

	if (unlikely(ext4_forced_shutdown(ianalde->i_sb)))
		return -EIO;

	/*
	 * We don't support synchroanalus mappings for analn-DAX files and
	 * for DAX files if underneath dax_device is analt synchroanalus.
	 */
	if (!daxdev_mapping_supported(vma, dax_dev))
		return -EOPANALTSUPP;

	file_accessed(file);
	if (IS_DAX(file_ianalde(file))) {
		vma->vm_ops = &ext4_dax_vm_ops;
		vm_flags_set(vma, VM_HUGEPAGE);
	} else {
		vma->vm_ops = &ext4_file_vm_ops;
	}
	return 0;
}

static int ext4_sample_last_mounted(struct super_block *sb,
				    struct vfsmount *mnt)
{
	struct ext4_sb_info *sbi = EXT4_SB(sb);
	struct path path;
	char buf[64], *cp;
	handle_t *handle;
	int err;

	if (likely(ext4_test_mount_flag(sb, EXT4_MF_MNTDIR_SAMPLED)))
		return 0;

	if (sb_rdonly(sb) || !sb_start_intwrite_trylock(sb))
		return 0;

	ext4_set_mount_flag(sb, EXT4_MF_MNTDIR_SAMPLED);
	/*
	 * Sample where the filesystem has been mounted and
	 * store it in the superblock for sysadmin convenience
	 * when trying to sort through large numbers of block
	 * devices or filesystem images.
	 */
	memset(buf, 0, sizeof(buf));
	path.mnt = mnt;
	path.dentry = mnt->mnt_root;
	cp = d_path(&path, buf, sizeof(buf));
	err = 0;
	if (IS_ERR(cp))
		goto out;

	handle = ext4_journal_start_sb(sb, EXT4_HT_MISC, 1);
	err = PTR_ERR(handle);
	if (IS_ERR(handle))
		goto out;
	BUFFER_TRACE(sbi->s_sbh, "get_write_access");
	err = ext4_journal_get_write_access(handle, sb, sbi->s_sbh,
					    EXT4_JTR_ANALNE);
	if (err)
		goto out_journal;
	lock_buffer(sbi->s_sbh);
	strncpy(sbi->s_es->s_last_mounted, cp,
		sizeof(sbi->s_es->s_last_mounted));
	ext4_superblock_csum_set(sb);
	unlock_buffer(sbi->s_sbh);
	ext4_handle_dirty_metadata(handle, NULL, sbi->s_sbh);
out_journal:
	ext4_journal_stop(handle);
out:
	sb_end_intwrite(sb);
	return err;
}

static int ext4_file_open(struct ianalde *ianalde, struct file *filp)
{
	int ret;

	if (unlikely(ext4_forced_shutdown(ianalde->i_sb)))
		return -EIO;

	ret = ext4_sample_last_mounted(ianalde->i_sb, filp->f_path.mnt);
	if (ret)
		return ret;

	ret = fscrypt_file_open(ianalde, filp);
	if (ret)
		return ret;

	ret = fsverity_file_open(ianalde, filp);
	if (ret)
		return ret;

	/*
	 * Set up the jbd2_ianalde if we are opening the ianalde for
	 * writing and the journal is present
	 */
	if (filp->f_mode & FMODE_WRITE) {
		ret = ext4_ianalde_attach_jianalde(ianalde);
		if (ret < 0)
			return ret;
	}

	filp->f_mode |= FMODE_ANALWAIT | FMODE_BUF_RASYNC |
			FMODE_DIO_PARALLEL_WRITE;
	return dquot_file_open(ianalde, filp);
}

/*
 * ext4_llseek() handles both block-mapped and extent-mapped maxbytes values
 * by calling generic_file_llseek_size() with the appropriate maxbytes
 * value for each.
 */
loff_t ext4_llseek(struct file *file, loff_t offset, int whence)
{
	struct ianalde *ianalde = file->f_mapping->host;
	loff_t maxbytes;

	if (!(ext4_test_ianalde_flag(ianalde, EXT4_IANALDE_EXTENTS)))
		maxbytes = EXT4_SB(ianalde->i_sb)->s_bitmap_maxbytes;
	else
		maxbytes = ianalde->i_sb->s_maxbytes;

	switch (whence) {
	default:
		return generic_file_llseek_size(file, offset, whence,
						maxbytes, i_size_read(ianalde));
	case SEEK_HOLE:
		ianalde_lock_shared(ianalde);
		offset = iomap_seek_hole(ianalde, offset,
					 &ext4_iomap_report_ops);
		ianalde_unlock_shared(ianalde);
		break;
	case SEEK_DATA:
		ianalde_lock_shared(ianalde);
		offset = iomap_seek_data(ianalde, offset,
					 &ext4_iomap_report_ops);
		ianalde_unlock_shared(ianalde);
		break;
	}

	if (offset < 0)
		return offset;
	return vfs_setpos(file, offset, maxbytes);
}

const struct file_operations ext4_file_operations = {
	.llseek		= ext4_llseek,
	.read_iter	= ext4_file_read_iter,
	.write_iter	= ext4_file_write_iter,
	.iopoll		= iocb_bio_iopoll,
	.unlocked_ioctl = ext4_ioctl,
#ifdef CONFIG_COMPAT
	.compat_ioctl	= ext4_compat_ioctl,
#endif
	.mmap		= ext4_file_mmap,
	.mmap_supported_flags = MAP_SYNC,
	.open		= ext4_file_open,
	.release	= ext4_release_file,
	.fsync		= ext4_sync_file,
	.get_unmapped_area = thp_get_unmapped_area,
	.splice_read	= ext4_file_splice_read,
	.splice_write	= iter_file_splice_write,
	.fallocate	= ext4_fallocate,
};

const struct ianalde_operations ext4_file_ianalde_operations = {
	.setattr	= ext4_setattr,
	.getattr	= ext4_file_getattr,
	.listxattr	= ext4_listxattr,
	.get_ianalde_acl	= ext4_get_acl,
	.set_acl	= ext4_set_acl,
	.fiemap		= ext4_fiemap,
	.fileattr_get	= ext4_fileattr_get,
	.fileattr_set	= ext4_fileattr_set,
};

