// SPDX-License-Identifier: GPL-2.0
/*
 *  linux/fs/ext2/file.c
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
 *  ext2 fs regular file handling primitives
 *
 *  64-bit file support on 64-bit platforms by Jakub Jelinek
 * 	(jj@sunsite.ms.mff.cuni.cz)
 */

#include <linux/time.h>
#include <linux/pagemap.h>
#include <linux/dax.h>
#include <linux/quotaops.h>
#include <linux/iomap.h>
#include <linux/uio.h>
#include <linux/buffer_head.h>
#include "ext2.h"
#include "xattr.h"
#include "acl.h"
#include "trace.h"

#ifdef CONFIG_FS_DAX
static ssize_t ext2_dax_read_iter(struct kiocb *iocb, struct iov_iter *to)
{
	struct inode *inode = iocb->ki_filp->f_mapping->host;
	ssize_t ret;

	if (!iov_iter_count(to))
		return 0; /* skip atime */

	inode_lock_shared(inode);
	ret = dax_iomap_rw(iocb, to, &ext2_iomap_ops);
	inode_unlock_shared(inode);

	file_accessed(iocb->ki_filp);
	return ret;
}

static ssize_t ext2_dax_write_iter(struct kiocb *iocb, struct iov_iter *from)
{
	struct file *file = iocb->ki_filp;
	struct inode *inode = file->f_mapping->host;
	ssize_t ret;

	inode_lock(inode);
	ret = generic_write_checks(iocb, from);
	if (ret <= 0)
		goto out_unlock;
	ret = file_remove_privs(file);
	if (ret)
		goto out_unlock;
	ret = file_update_time(file);
	if (ret)
		goto out_unlock;

	ret = dax_iomap_rw(iocb, from, &ext2_iomap_ops);
	if (ret > 0 && iocb->ki_pos > i_size_read(inode)) {
		i_size_write(inode, iocb->ki_pos);
		mark_inode_dirty(inode);
	}

out_unlock:
	inode_unlock(inode);
	if (ret > 0)
		ret = generic_write_sync(iocb, ret);
	return ret;
}

/*
 * The lock ordering for ext2 DAX fault paths is:
 *
 * mmap_lock (MM)
 *   sb_start_pagefault (vfs, freeze)
 *     address_space->invalidate_lock
 *       address_space->i_mmap_rwsem or page_lock (mutually exclusive in DAX)
 *         ext2_inode_info->truncate_mutex
 *
 * The default page_lock and i_size verification done by non-DAX fault paths
 * is sufficient because ext2 doesn't support hole punching.
 */
static vm_fault_t ext2_dax_fault(struct vm_fault *vmf)
{
	struct inode *inode = file_inode(vmf->vma->vm_file);
	vm_fault_t ret;
	bool write = (vmf->flags & FAULT_FLAG_WRITE) &&
		(vmf->vma->vm_flags & VM_SHARED);

	if (write) {
		sb_start_pagefault(inode->i_sb);
		file_update_time(vmf->vma->vm_file);
	}
	filemap_invalidate_lock_shared(inode->i_mapping);

	ret = dax_iomap_fault(vmf, PE_SIZE_PTE, NULL, NULL, &ext2_iomap_ops);

	filemap_invalidate_unlock_shared(inode->i_mapping);
	if (write)
		sb_end_pagefault(inode->i_sb);
	return ret;
}

static const struct vm_operations_struct ext2_dax_vm_ops = {
	.fault		= ext2_dax_fault,
	/*
	 * .huge_fault is not supported for DAX because allocation in ext2
	 * cannot be reliably aligned to huge page sizes and so pmd faults
	 * will always fail and fail back to regular faults.
	 */
	.page_mkwrite	= ext2_dax_fault,
	.pfn_mkwrite	= ext2_dax_fault,
};

static int ext2_file_mmap(struct file *file, struct vm_area_struct *vma)
{
	if (!IS_DAX(file_inode(file)))
		return generic_file_mmap(file, vma);

	file_accessed(file);
	vma->vm_ops = &ext2_dax_vm_ops;
	return 0;
}
#else
#define ext2_file_mmap	generic_file_mmap
#endif

/*
 * Called when filp is released. This happens when all file descriptors
 * for a single struct file are closed. Note that different open() calls
 * for the same file yield different struct file structures.
 */
static int ext2_release_file (struct inode * inode, struct file * filp)
{
	if (filp->f_mode & FMODE_WRITE) {
		mutex_lock(&EXT2_I(inode)->truncate_mutex);
		ext2_discard_reservation(inode);
		mutex_unlock(&EXT2_I(inode)->truncate_mutex);
	}
	return 0;
}

int ext2_fsync(struct file *file, loff_t start, loff_t end, int datasync)
{
	int ret;
	struct super_block *sb = file->f_mapping->host->i_sb;

	ret = generic_buffers_fsync(file, start, end, datasync);
	if (ret == -EIO)
		/* We don't really know where the IO error happened... */
		ext2_error(sb, __func__,
			   "detected IO error when writing metadata buffers");
	return ret;
}

static ssize_t ext2_dio_read_iter(struct kiocb *iocb, struct iov_iter *to)
{
	struct file *file = iocb->ki_filp;
	struct inode *inode = file->f_mapping->host;
	ssize_t ret;

	trace_ext2_dio_read_begin(iocb, to, 0);
	inode_lock_shared(inode);
	ret = iomap_dio_rw(iocb, to, &ext2_iomap_ops, NULL, 0, NULL, 0);
	inode_unlock_shared(inode);
	trace_ext2_dio_read_end(iocb, to, ret);

	return ret;
}

static int ext2_dio_write_end_io(struct kiocb *iocb, ssize_t size,
				 int error, unsigned int flags)
{
	loff_t pos = iocb->ki_pos;
	struct inode *inode = file_inode(iocb->ki_filp);

	if (error)
		goto out;

	/*
	 * If we are extending the file, we have to update i_size here before
	 * page cache gets invalidated in iomap_dio_rw(). This prevents racing
	 * buffered reads from zeroing out too much from page cache pages.
	 * Note that all extending writes always happens synchronously with
	 * inode lock held by ext2_dio_write_iter(). So it is safe to update
	 * inode size here for extending file writes.
	 */
	pos += size;
	if (pos > i_size_read(inode)) {
		i_size_write(inode, pos);
		mark_inode_dirty(inode);
	}
out:
	trace_ext2_dio_write_endio(iocb, size, error);
	return error;
}

static const struct iomap_dio_ops ext2_dio_write_ops = {
	.end_io = ext2_dio_write_end_io,
};

static ssize_t ext2_dio_write_iter(struct kiocb *iocb, struct iov_iter *from)
{
	struct file *file = iocb->ki_filp;
	struct inode *inode = file->f_mapping->host;
	ssize_t ret;
	unsigned int flags = 0;
	unsigned long blocksize = inode->i_sb->s_blocksize;
	loff_t offset = iocb->ki_pos;
	loff_t count = iov_iter_count(from);
	ssize_t status = 0;

	trace_ext2_dio_write_begin(iocb, from, 0);
	inode_lock(inode);
	ret = generic_write_checks(iocb, from);
	if (ret <= 0)
		goto out_unlock;

	ret = kiocb_modified(iocb);
	if (ret)
		goto out_unlock;

	/* use IOMAP_DIO_FORCE_WAIT for unaligned or extending writes */
	if (iocb->ki_pos + iov_iter_count(from) > i_size_read(inode) ||
	   (!IS_ALIGNED(iocb->ki_pos | iov_iter_alignment(from), blocksize)))
		flags |= IOMAP_DIO_FORCE_WAIT;

	ret = iomap_dio_rw(iocb, from, &ext2_iomap_ops, &ext2_dio_write_ops,
			   flags, NULL, 0);

	/* ENOTBLK is magic return value for fallback to buffered-io */
	if (ret == -ENOTBLK)
		ret = 0;

	if (ret < 0 && ret != -EIOCBQUEUED)
		ext2_write_failed(inode->i_mapping, offset + count);

	/* handle case for partial write and for fallback to buffered write */
	if (ret >= 0 && iov_iter_count(from)) {
		loff_t pos, endbyte;
		int ret2;

		iocb->ki_flags &= ~IOCB_DIRECT;
		pos = iocb->ki_pos;
		status = generic_perform_write(iocb, from);
		if (unlikely(status < 0)) {
			ret = status;
			goto out_unlock;
		}

		iocb->ki_pos += status;
		ret += status;
		endbyte = pos + status - 1;
		ret2 = filemap_write_and_wait_range(inode->i_mapping, pos,
						    endbyte);
		if (!ret2)
			invalidate_mapping_pages(inode->i_mapping,
						 pos >> PAGE_SHIFT,
						 endbyte >> PAGE_SHIFT);
		if (ret > 0)
			generic_write_sync(iocb, ret);
	}

out_unlock:
	inode_unlock(inode);
	if (status)
		trace_ext2_dio_write_buff_end(iocb, from, status);
	trace_ext2_dio_write_end(iocb, from, ret);
	return ret;
}

static ssize_t ext2_file_read_iter(struct kiocb *iocb, struct iov_iter *to)
{
#ifdef CONFIG_FS_DAX
	if (IS_DAX(iocb->ki_filp->f_mapping->host))
		return ext2_dax_read_iter(iocb, to);
#endif
	if (iocb->ki_flags & IOCB_DIRECT)
		return ext2_dio_read_iter(iocb, to);

	return generic_file_read_iter(iocb, to);
}

static ssize_t ext2_file_write_iter(struct kiocb *iocb, struct iov_iter *from)
{
#ifdef CONFIG_FS_DAX
	if (IS_DAX(iocb->ki_filp->f_mapping->host))
		return ext2_dax_write_iter(iocb, from);
#endif
	if (iocb->ki_flags & IOCB_DIRECT)
		return ext2_dio_write_iter(iocb, from);

	return generic_file_write_iter(iocb, from);
}

const struct file_operations ext2_file_operations = {
	.llseek		= generic_file_llseek,
	.read_iter	= ext2_file_read_iter,
	.write_iter	= ext2_file_write_iter,
	.unlocked_ioctl = ext2_ioctl,
#ifdef CONFIG_COMPAT
	.compat_ioctl	= ext2_compat_ioctl,
#endif
	.mmap		= ext2_file_mmap,
	.open		= dquot_file_open,
	.release	= ext2_release_file,
	.fsync		= ext2_fsync,
	.get_unmapped_area = thp_get_unmapped_area,
	.splice_read	= filemap_splice_read,
	.splice_write	= iter_file_splice_write,
};

const struct inode_operations ext2_file_inode_operations = {
	.listxattr	= ext2_listxattr,
	.getattr	= ext2_getattr,
	.setattr	= ext2_setattr,
	.get_inode_acl	= ext2_get_acl,
	.set_acl	= ext2_set_acl,
	.fiemap		= ext2_fiemap,
	.fileattr_get	= ext2_fileattr_get,
	.fileattr_set	= ext2_fileattr_set,
};
