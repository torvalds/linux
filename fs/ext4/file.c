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

static bool ext4_dio_supported(struct iyesde *iyesde)
{
	if (IS_ENABLED(CONFIG_FS_ENCRYPTION) && IS_ENCRYPTED(iyesde))
		return false;
	if (fsverity_active(iyesde))
		return false;
	if (ext4_should_journal_data(iyesde))
		return false;
	if (ext4_has_inline_data(iyesde))
		return false;
	return true;
}

static ssize_t ext4_dio_read_iter(struct kiocb *iocb, struct iov_iter *to)
{
	ssize_t ret;
	struct iyesde *iyesde = file_iyesde(iocb->ki_filp);

	if (iocb->ki_flags & IOCB_NOWAIT) {
		if (!iyesde_trylock_shared(iyesde))
			return -EAGAIN;
	} else {
		iyesde_lock_shared(iyesde);
	}

	if (!ext4_dio_supported(iyesde)) {
		iyesde_unlock_shared(iyesde);
		/*
		 * Fallback to buffered I/O if the operation being performed on
		 * the iyesde is yest supported by direct I/O. The IOCB_DIRECT
		 * flag needs to be cleared here in order to ensure that the
		 * direct I/O path within generic_file_read_iter() is yest
		 * taken.
		 */
		iocb->ki_flags &= ~IOCB_DIRECT;
		return generic_file_read_iter(iocb, to);
	}

	ret = iomap_dio_rw(iocb, to, &ext4_iomap_ops, NULL,
			   is_sync_kiocb(iocb));
	iyesde_unlock_shared(iyesde);

	file_accessed(iocb->ki_filp);
	return ret;
}

#ifdef CONFIG_FS_DAX
static ssize_t ext4_dax_read_iter(struct kiocb *iocb, struct iov_iter *to)
{
	struct iyesde *iyesde = file_iyesde(iocb->ki_filp);
	ssize_t ret;

	if (!iyesde_trylock_shared(iyesde)) {
		if (iocb->ki_flags & IOCB_NOWAIT)
			return -EAGAIN;
		iyesde_lock_shared(iyesde);
	}
	/*
	 * Recheck under iyesde lock - at this point we are sure it canyest
	 * change anymore
	 */
	if (!IS_DAX(iyesde)) {
		iyesde_unlock_shared(iyesde);
		/* Fallback to buffered IO in case we canyest support DAX */
		return generic_file_read_iter(iocb, to);
	}
	ret = dax_iomap_rw(iocb, to, &ext4_iomap_ops);
	iyesde_unlock_shared(iyesde);

	file_accessed(iocb->ki_filp);
	return ret;
}
#endif

static ssize_t ext4_file_read_iter(struct kiocb *iocb, struct iov_iter *to)
{
	struct iyesde *iyesde = file_iyesde(iocb->ki_filp);

	if (unlikely(ext4_forced_shutdown(EXT4_SB(iyesde->i_sb))))
		return -EIO;

	if (!iov_iter_count(to))
		return 0; /* skip atime */

#ifdef CONFIG_FS_DAX
	if (IS_DAX(iyesde))
		return ext4_dax_read_iter(iocb, to);
#endif
	if (iocb->ki_flags & IOCB_DIRECT)
		return ext4_dio_read_iter(iocb, to);

	return generic_file_read_iter(iocb, to);
}

/*
 * Called when an iyesde is released. Note that this is different
 * from ext4_file_open: open gets called at every open, but release
 * gets called only when /all/ the files are closed.
 */
static int ext4_release_file(struct iyesde *iyesde, struct file *filp)
{
	if (ext4_test_iyesde_state(iyesde, EXT4_STATE_DA_ALLOC_CLOSE)) {
		ext4_alloc_da_blocks(iyesde);
		ext4_clear_iyesde_state(iyesde, EXT4_STATE_DA_ALLOC_CLOSE);
	}
	/* if we are the last writer on the iyesde, drop the block reservation */
	if ((filp->f_mode & FMODE_WRITE) &&
			(atomic_read(&iyesde->i_writecount) == 1) &&
		        !EXT4_I(iyesde)->i_reserved_data_blocks)
	{
		down_write(&EXT4_I(iyesde)->i_data_sem);
		ext4_discard_preallocations(iyesde);
		up_write(&EXT4_I(iyesde)->i_data_sem);
	}
	if (is_dx(iyesde) && filp->private_data)
		ext4_htree_free_dir_info(filp->private_data);

	return 0;
}

/*
 * This tests whether the IO in question is block-aligned or yest.
 * Ext4 utilizes unwritten extents when hole-filling during direct IO, and they
 * are converted to written only after the IO is complete.  Until they are
 * mapped, these blocks appear as holes, so dio_zero_block() will assume that
 * it needs to zero out portions of the start and/or end block.  If 2 AIO
 * threads are at work on the same unwritten block, they must be synchronized
 * or one thread will zero the other's data, causing corruption.
 */
static int
ext4_unaligned_aio(struct iyesde *iyesde, struct iov_iter *from, loff_t pos)
{
	struct super_block *sb = iyesde->i_sb;
	int blockmask = sb->s_blocksize - 1;

	if (pos >= ALIGN(i_size_read(iyesde), sb->s_blocksize))
		return 0;

	if ((pos | iov_iter_alignment(from)) & blockmask)
		return 1;

	return 0;
}

/* Is IO overwriting allocated and initialized blocks? */
static bool ext4_overwrite_io(struct iyesde *iyesde, loff_t pos, loff_t len)
{
	struct ext4_map_blocks map;
	unsigned int blkbits = iyesde->i_blkbits;
	int err, blklen;

	if (pos + len > i_size_read(iyesde))
		return false;

	map.m_lblk = pos >> blkbits;
	map.m_len = EXT4_MAX_BLOCKS(len, pos, blkbits);
	blklen = map.m_len;

	err = ext4_map_blocks(NULL, iyesde, &map, 0);
	/*
	 * 'err==len' means that all of the blocks have been preallocated,
	 * regardless of whether they have been initialized or yest. To exclude
	 * unwritten extents, we need to check m_flags.
	 */
	return err == blklen && (map.m_flags & EXT4_MAP_MAPPED);
}

static ssize_t ext4_write_checks(struct kiocb *iocb, struct iov_iter *from)
{
	struct iyesde *iyesde = file_iyesde(iocb->ki_filp);
	ssize_t ret;

	if (unlikely(IS_IMMUTABLE(iyesde)))
		return -EPERM;

	ret = generic_write_checks(iocb, from);
	if (ret <= 0)
		return ret;

	/*
	 * If we have encountered a bitmap-format file, the size limit
	 * is smaller than s_maxbytes, which is for extent-mapped files.
	 */
	if (!(ext4_test_iyesde_flag(iyesde, EXT4_INODE_EXTENTS))) {
		struct ext4_sb_info *sbi = EXT4_SB(iyesde->i_sb);

		if (iocb->ki_pos >= sbi->s_bitmap_maxbytes)
			return -EFBIG;
		iov_iter_truncate(from, sbi->s_bitmap_maxbytes - iocb->ki_pos);
	}

	ret = file_modified(iocb->ki_filp);
	if (ret)
		return ret;

	return iov_iter_count(from);
}

static ssize_t ext4_buffered_write_iter(struct kiocb *iocb,
					struct iov_iter *from)
{
	ssize_t ret;
	struct iyesde *iyesde = file_iyesde(iocb->ki_filp);

	if (iocb->ki_flags & IOCB_NOWAIT)
		return -EOPNOTSUPP;

	iyesde_lock(iyesde);
	ret = ext4_write_checks(iocb, from);
	if (ret <= 0)
		goto out;

	current->backing_dev_info = iyesde_to_bdi(iyesde);
	ret = generic_perform_write(iocb->ki_filp, from, iocb->ki_pos);
	current->backing_dev_info = NULL;

out:
	iyesde_unlock(iyesde);
	if (likely(ret > 0)) {
		iocb->ki_pos += ret;
		ret = generic_write_sync(iocb, ret);
	}

	return ret;
}

static ssize_t ext4_handle_iyesde_extension(struct iyesde *iyesde, loff_t offset,
					   ssize_t written, size_t count)
{
	handle_t *handle;
	bool truncate = false;
	u8 blkbits = iyesde->i_blkbits;
	ext4_lblk_t written_blk, end_blk;

	/*
	 * Note that EXT4_I(iyesde)->i_disksize can get extended up to
	 * iyesde->i_size while the I/O was running due to writeback of delalloc
	 * blocks. But, the code in ext4_iomap_alloc() is careful to use
	 * zeroed/unwritten extents if this is possible; thus we won't leave
	 * uninitialized blocks in a file even if we didn't succeed in writing
	 * as much as we intended.
	 */
	WARN_ON_ONCE(i_size_read(iyesde) < EXT4_I(iyesde)->i_disksize);
	if (offset + count <= EXT4_I(iyesde)->i_disksize) {
		/*
		 * We need to ensure that the iyesde is removed from the orphan
		 * list if it has been added prematurely, due to writeback of
		 * delalloc blocks.
		 */
		if (!list_empty(&EXT4_I(iyesde)->i_orphan) && iyesde->i_nlink) {
			handle = ext4_journal_start(iyesde, EXT4_HT_INODE, 2);

			if (IS_ERR(handle)) {
				ext4_orphan_del(NULL, iyesde);
				return PTR_ERR(handle);
			}

			ext4_orphan_del(handle, iyesde);
			ext4_journal_stop(handle);
		}

		return written;
	}

	if (written < 0)
		goto truncate;

	handle = ext4_journal_start(iyesde, EXT4_HT_INODE, 2);
	if (IS_ERR(handle)) {
		written = PTR_ERR(handle);
		goto truncate;
	}

	if (ext4_update_iyesde_size(iyesde, offset + written))
		ext4_mark_iyesde_dirty(handle, iyesde);

	/*
	 * We may need to truncate allocated but yest written blocks beyond EOF.
	 */
	written_blk = ALIGN(offset + written, 1 << blkbits);
	end_blk = ALIGN(offset + count, 1 << blkbits);
	if (written_blk < end_blk && ext4_can_truncate(iyesde))
		truncate = true;

	/*
	 * Remove the iyesde from the orphan list if it has been extended and
	 * everything went OK.
	 */
	if (!truncate && iyesde->i_nlink)
		ext4_orphan_del(handle, iyesde);
	ext4_journal_stop(handle);

	if (truncate) {
truncate:
		ext4_truncate_failed_write(iyesde);
		/*
		 * If the truncate operation failed early, then the iyesde may
		 * still be on the orphan list. In that case, we need to try
		 * remove the iyesde from the in-memory linked list.
		 */
		if (iyesde->i_nlink)
			ext4_orphan_del(NULL, iyesde);
	}

	return written;
}

static int ext4_dio_write_end_io(struct kiocb *iocb, ssize_t size,
				 int error, unsigned int flags)
{
	loff_t offset = iocb->ki_pos;
	struct iyesde *iyesde = file_iyesde(iocb->ki_filp);

	if (error)
		return error;

	if (size && flags & IOMAP_DIO_UNWRITTEN)
		return ext4_convert_unwritten_extents(NULL, iyesde,
						      offset, size);

	return 0;
}

static const struct iomap_dio_ops ext4_dio_write_ops = {
	.end_io = ext4_dio_write_end_io,
};

static ssize_t ext4_dio_write_iter(struct kiocb *iocb, struct iov_iter *from)
{
	ssize_t ret;
	size_t count;
	loff_t offset;
	handle_t *handle;
	struct iyesde *iyesde = file_iyesde(iocb->ki_filp);
	bool extend = false, overwrite = false, unaligned_aio = false;

	if (iocb->ki_flags & IOCB_NOWAIT) {
		if (!iyesde_trylock(iyesde))
			return -EAGAIN;
	} else {
		iyesde_lock(iyesde);
	}

	if (!ext4_dio_supported(iyesde)) {
		iyesde_unlock(iyesde);
		/*
		 * Fallback to buffered I/O if the iyesde does yest support
		 * direct I/O.
		 */
		return ext4_buffered_write_iter(iocb, from);
	}

	ret = ext4_write_checks(iocb, from);
	if (ret <= 0) {
		iyesde_unlock(iyesde);
		return ret;
	}

	/*
	 * Unaligned asynchroyesus direct I/O must be serialized among each
	 * other as the zeroing of partial blocks of two competing unaligned
	 * asynchroyesus direct I/O writes can result in data corruption.
	 */
	offset = iocb->ki_pos;
	count = iov_iter_count(from);
	if (ext4_test_iyesde_flag(iyesde, EXT4_INODE_EXTENTS) &&
	    !is_sync_kiocb(iocb) && ext4_unaligned_aio(iyesde, from, offset)) {
		unaligned_aio = true;
		iyesde_dio_wait(iyesde);
	}

	/*
	 * Determine whether the I/O will overwrite allocated and initialized
	 * blocks. If so, check to see whether it is possible to take the
	 * dioread_yeslock path.
	 */
	if (!unaligned_aio && ext4_overwrite_io(iyesde, offset, count) &&
	    ext4_should_dioread_yeslock(iyesde)) {
		overwrite = true;
		downgrade_write(&iyesde->i_rwsem);
	}

	if (offset + count > EXT4_I(iyesde)->i_disksize) {
		handle = ext4_journal_start(iyesde, EXT4_HT_INODE, 2);
		if (IS_ERR(handle)) {
			ret = PTR_ERR(handle);
			goto out;
		}

		ret = ext4_orphan_add(handle, iyesde);
		if (ret) {
			ext4_journal_stop(handle);
			goto out;
		}

		extend = true;
		ext4_journal_stop(handle);
	}

	ret = iomap_dio_rw(iocb, from, &ext4_iomap_ops, &ext4_dio_write_ops,
			   is_sync_kiocb(iocb) || unaligned_aio || extend);

	if (extend)
		ret = ext4_handle_iyesde_extension(iyesde, offset, ret, count);

out:
	if (overwrite)
		iyesde_unlock_shared(iyesde);
	else
		iyesde_unlock(iyesde);

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
	struct iyesde *iyesde = file_iyesde(iocb->ki_filp);

	if (!iyesde_trylock(iyesde)) {
		if (iocb->ki_flags & IOCB_NOWAIT)
			return -EAGAIN;
		iyesde_lock(iyesde);
	}

	ret = ext4_write_checks(iocb, from);
	if (ret <= 0)
		goto out;

	offset = iocb->ki_pos;
	count = iov_iter_count(from);

	if (offset + count > EXT4_I(iyesde)->i_disksize) {
		handle = ext4_journal_start(iyesde, EXT4_HT_INODE, 2);
		if (IS_ERR(handle)) {
			ret = PTR_ERR(handle);
			goto out;
		}

		ret = ext4_orphan_add(handle, iyesde);
		if (ret) {
			ext4_journal_stop(handle);
			goto out;
		}

		extend = true;
		ext4_journal_stop(handle);
	}

	ret = dax_iomap_rw(iocb, from, &ext4_iomap_ops);

	if (extend)
		ret = ext4_handle_iyesde_extension(iyesde, offset, ret, count);
out:
	iyesde_unlock(iyesde);
	if (ret > 0)
		ret = generic_write_sync(iocb, ret);
	return ret;
}
#endif

static ssize_t
ext4_file_write_iter(struct kiocb *iocb, struct iov_iter *from)
{
	struct iyesde *iyesde = file_iyesde(iocb->ki_filp);

	if (unlikely(ext4_forced_shutdown(EXT4_SB(iyesde->i_sb))))
		return -EIO;

#ifdef CONFIG_FS_DAX
	if (IS_DAX(iyesde))
		return ext4_dax_write_iter(iocb, from);
#endif
	if (iocb->ki_flags & IOCB_DIRECT)
		return ext4_dio_write_iter(iocb, from);

	return ext4_buffered_write_iter(iocb, from);
}

#ifdef CONFIG_FS_DAX
static vm_fault_t ext4_dax_huge_fault(struct vm_fault *vmf,
		enum page_entry_size pe_size)
{
	int error = 0;
	vm_fault_t result;
	int retries = 0;
	handle_t *handle = NULL;
	struct iyesde *iyesde = file_iyesde(vmf->vma->vm_file);
	struct super_block *sb = iyesde->i_sb;

	/*
	 * We have to distinguish real writes from writes which will result in a
	 * COW page; COW writes should *yest* poke the journal (the file will yest
	 * be changed). Doing so would cause unintended failures when mounted
	 * read-only.
	 *
	 * We check for VM_SHARED rather than vmf->cow_page since the latter is
	 * unset for pe_size != PE_SIZE_PTE (i.e. only in do_cow_fault); for
	 * other sizes, dax_iomap_fault will handle splitting / fallback so that
	 * we eventually come back with a COW page.
	 */
	bool write = (vmf->flags & FAULT_FLAG_WRITE) &&
		(vmf->vma->vm_flags & VM_SHARED);
	pfn_t pfn;

	if (write) {
		sb_start_pagefault(sb);
		file_update_time(vmf->vma->vm_file);
		down_read(&EXT4_I(iyesde)->i_mmap_sem);
retry:
		handle = ext4_journal_start_sb(sb, EXT4_HT_WRITE_PAGE,
					       EXT4_DATA_TRANS_BLOCKS(sb));
		if (IS_ERR(handle)) {
			up_read(&EXT4_I(iyesde)->i_mmap_sem);
			sb_end_pagefault(sb);
			return VM_FAULT_SIGBUS;
		}
	} else {
		down_read(&EXT4_I(iyesde)->i_mmap_sem);
	}
	result = dax_iomap_fault(vmf, pe_size, &pfn, &error, &ext4_iomap_ops);
	if (write) {
		ext4_journal_stop(handle);

		if ((result & VM_FAULT_ERROR) && error == -ENOSPC &&
		    ext4_should_retry_alloc(sb, &retries))
			goto retry;
		/* Handling synchroyesus page fault? */
		if (result & VM_FAULT_NEEDDSYNC)
			result = dax_finish_sync_fault(vmf, pe_size, pfn);
		up_read(&EXT4_I(iyesde)->i_mmap_sem);
		sb_end_pagefault(sb);
	} else {
		up_read(&EXT4_I(iyesde)->i_mmap_sem);
	}

	return result;
}

static vm_fault_t ext4_dax_fault(struct vm_fault *vmf)
{
	return ext4_dax_huge_fault(vmf, PE_SIZE_PTE);
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
	.fault		= ext4_filemap_fault,
	.map_pages	= filemap_map_pages,
	.page_mkwrite   = ext4_page_mkwrite,
};

static int ext4_file_mmap(struct file *file, struct vm_area_struct *vma)
{
	struct iyesde *iyesde = file->f_mapping->host;
	struct ext4_sb_info *sbi = EXT4_SB(iyesde->i_sb);
	struct dax_device *dax_dev = sbi->s_daxdev;

	if (unlikely(ext4_forced_shutdown(sbi)))
		return -EIO;

	/*
	 * We don't support synchroyesus mappings for yesn-DAX files and
	 * for DAX files if underneath dax_device is yest synchroyesus.
	 */
	if (!daxdev_mapping_supported(vma, dax_dev))
		return -EOPNOTSUPP;

	file_accessed(file);
	if (IS_DAX(file_iyesde(file))) {
		vma->vm_ops = &ext4_dax_vm_ops;
		vma->vm_flags |= VM_HUGEPAGE;
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

	if (likely(sbi->s_mount_flags & EXT4_MF_MNTDIR_SAMPLED))
		return 0;

	if (sb_rdonly(sb) || !sb_start_intwrite_trylock(sb))
		return 0;

	sbi->s_mount_flags |= EXT4_MF_MNTDIR_SAMPLED;
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
	err = ext4_journal_get_write_access(handle, sbi->s_sbh);
	if (err)
		goto out_journal;
	strlcpy(sbi->s_es->s_last_mounted, cp,
		sizeof(sbi->s_es->s_last_mounted));
	ext4_handle_dirty_super(handle, sb);
out_journal:
	ext4_journal_stop(handle);
out:
	sb_end_intwrite(sb);
	return err;
}

static int ext4_file_open(struct iyesde * iyesde, struct file * filp)
{
	int ret;

	if (unlikely(ext4_forced_shutdown(EXT4_SB(iyesde->i_sb))))
		return -EIO;

	ret = ext4_sample_last_mounted(iyesde->i_sb, filp->f_path.mnt);
	if (ret)
		return ret;

	ret = fscrypt_file_open(iyesde, filp);
	if (ret)
		return ret;

	ret = fsverity_file_open(iyesde, filp);
	if (ret)
		return ret;

	/*
	 * Set up the jbd2_iyesde if we are opening the iyesde for
	 * writing and the journal is present
	 */
	if (filp->f_mode & FMODE_WRITE) {
		ret = ext4_iyesde_attach_jiyesde(iyesde);
		if (ret < 0)
			return ret;
	}

	filp->f_mode |= FMODE_NOWAIT;
	return dquot_file_open(iyesde, filp);
}

/*
 * ext4_llseek() handles both block-mapped and extent-mapped maxbytes values
 * by calling generic_file_llseek_size() with the appropriate maxbytes
 * value for each.
 */
loff_t ext4_llseek(struct file *file, loff_t offset, int whence)
{
	struct iyesde *iyesde = file->f_mapping->host;
	loff_t maxbytes;

	if (!(ext4_test_iyesde_flag(iyesde, EXT4_INODE_EXTENTS)))
		maxbytes = EXT4_SB(iyesde->i_sb)->s_bitmap_maxbytes;
	else
		maxbytes = iyesde->i_sb->s_maxbytes;

	switch (whence) {
	default:
		return generic_file_llseek_size(file, offset, whence,
						maxbytes, i_size_read(iyesde));
	case SEEK_HOLE:
		iyesde_lock_shared(iyesde);
		offset = iomap_seek_hole(iyesde, offset,
					 &ext4_iomap_report_ops);
		iyesde_unlock_shared(iyesde);
		break;
	case SEEK_DATA:
		iyesde_lock_shared(iyesde);
		offset = iomap_seek_data(iyesde, offset,
					 &ext4_iomap_report_ops);
		iyesde_unlock_shared(iyesde);
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
	.splice_read	= generic_file_splice_read,
	.splice_write	= iter_file_splice_write,
	.fallocate	= ext4_fallocate,
};

const struct iyesde_operations ext4_file_iyesde_operations = {
	.setattr	= ext4_setattr,
	.getattr	= ext4_file_getattr,
	.listxattr	= ext4_listxattr,
	.get_acl	= ext4_get_acl,
	.set_acl	= ext4_set_acl,
	.fiemap		= ext4_fiemap,
};

