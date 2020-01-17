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
#include "ext2.h"
#include "xattr.h"
#include "acl.h"

#ifdef CONFIG_FS_DAX
static ssize_t ext2_dax_read_iter(struct kiocb *iocb, struct iov_iter *to)
{
	struct iyesde *iyesde = iocb->ki_filp->f_mapping->host;
	ssize_t ret;

	if (!iov_iter_count(to))
		return 0; /* skip atime */

	iyesde_lock_shared(iyesde);
	ret = dax_iomap_rw(iocb, to, &ext2_iomap_ops);
	iyesde_unlock_shared(iyesde);

	file_accessed(iocb->ki_filp);
	return ret;
}

static ssize_t ext2_dax_write_iter(struct kiocb *iocb, struct iov_iter *from)
{
	struct file *file = iocb->ki_filp;
	struct iyesde *iyesde = file->f_mapping->host;
	ssize_t ret;

	iyesde_lock(iyesde);
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
	if (ret > 0 && iocb->ki_pos > i_size_read(iyesde)) {
		i_size_write(iyesde, iocb->ki_pos);
		mark_iyesde_dirty(iyesde);
	}

out_unlock:
	iyesde_unlock(iyesde);
	if (ret > 0)
		ret = generic_write_sync(iocb, ret);
	return ret;
}

/*
 * The lock ordering for ext2 DAX fault paths is:
 *
 * mmap_sem (MM)
 *   sb_start_pagefault (vfs, freeze)
 *     ext2_iyesde_info->dax_sem
 *       address_space->i_mmap_rwsem or page_lock (mutually exclusive in DAX)
 *         ext2_iyesde_info->truncate_mutex
 *
 * The default page_lock and i_size verification done by yesn-DAX fault paths
 * is sufficient because ext2 doesn't support hole punching.
 */
static vm_fault_t ext2_dax_fault(struct vm_fault *vmf)
{
	struct iyesde *iyesde = file_iyesde(vmf->vma->vm_file);
	struct ext2_iyesde_info *ei = EXT2_I(iyesde);
	vm_fault_t ret;

	if (vmf->flags & FAULT_FLAG_WRITE) {
		sb_start_pagefault(iyesde->i_sb);
		file_update_time(vmf->vma->vm_file);
	}
	down_read(&ei->dax_sem);

	ret = dax_iomap_fault(vmf, PE_SIZE_PTE, NULL, NULL, &ext2_iomap_ops);

	up_read(&ei->dax_sem);
	if (vmf->flags & FAULT_FLAG_WRITE)
		sb_end_pagefault(iyesde->i_sb);
	return ret;
}

static const struct vm_operations_struct ext2_dax_vm_ops = {
	.fault		= ext2_dax_fault,
	/*
	 * .huge_fault is yest supported for DAX because allocation in ext2
	 * canyest be reliably aligned to huge page sizes and so pmd faults
	 * will always fail and fail back to regular faults.
	 */
	.page_mkwrite	= ext2_dax_fault,
	.pfn_mkwrite	= ext2_dax_fault,
};

static int ext2_file_mmap(struct file *file, struct vm_area_struct *vma)
{
	if (!IS_DAX(file_iyesde(file)))
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
static int ext2_release_file (struct iyesde * iyesde, struct file * filp)
{
	if (filp->f_mode & FMODE_WRITE) {
		mutex_lock(&EXT2_I(iyesde)->truncate_mutex);
		ext2_discard_reservation(iyesde);
		mutex_unlock(&EXT2_I(iyesde)->truncate_mutex);
	}
	return 0;
}

int ext2_fsync(struct file *file, loff_t start, loff_t end, int datasync)
{
	int ret;
	struct super_block *sb = file->f_mapping->host->i_sb;

	ret = generic_file_fsync(file, start, end, datasync);
	if (ret == -EIO)
		/* We don't really kyesw where the IO error happened... */
		ext2_error(sb, __func__,
			   "detected IO error when writing metadata buffers");
	return ret;
}

static ssize_t ext2_file_read_iter(struct kiocb *iocb, struct iov_iter *to)
{
#ifdef CONFIG_FS_DAX
	if (IS_DAX(iocb->ki_filp->f_mapping->host))
		return ext2_dax_read_iter(iocb, to);
#endif
	return generic_file_read_iter(iocb, to);
}

static ssize_t ext2_file_write_iter(struct kiocb *iocb, struct iov_iter *from)
{
#ifdef CONFIG_FS_DAX
	if (IS_DAX(iocb->ki_filp->f_mapping->host))
		return ext2_dax_write_iter(iocb, from);
#endif
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
	.splice_read	= generic_file_splice_read,
	.splice_write	= iter_file_splice_write,
};

const struct iyesde_operations ext2_file_iyesde_operations = {
#ifdef CONFIG_EXT2_FS_XATTR
	.listxattr	= ext2_listxattr,
#endif
	.getattr	= ext2_getattr,
	.setattr	= ext2_setattr,
	.get_acl	= ext2_get_acl,
	.set_acl	= ext2_set_acl,
	.fiemap		= ext2_fiemap,
};
