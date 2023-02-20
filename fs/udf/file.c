/*
 * file.c
 *
 * PURPOSE
 *  File handling routines for the OSTA-UDF(tm) filesystem.
 *
 * COPYRIGHT
 *  This file is distributed under the terms of the GNU General Public
 *  License (GPL). Copies of the GPL can be obtained from:
 *    ftp://prep.ai.mit.edu/pub/gnu/GPL
 *  Each contributing author retains all rights to their own work.
 *
 *  (C) 1998-1999 Dave Boynton
 *  (C) 1998-2004 Ben Fennema
 *  (C) 1999-2000 Stelias Computing Inc
 *
 * HISTORY
 *
 *  10/02/98 dgb  Attempt to integrate into udf.o
 *  10/07/98      Switched to using generic_readpage, etc., like isofs
 *                And it works!
 *  12/06/98 blf  Added udf_file_read. uses generic_file_read for all cases but
 *                ICBTAG_FLAG_AD_IN_ICB.
 *  04/06/99      64 bit file handling on 32 bit systems taken from ext2 file.c
 *  05/12/99      Preliminary file write support
 */

#include "udfdecl.h"
#include <linux/fs.h>
#include <linux/uaccess.h>
#include <linux/kernel.h>
#include <linux/string.h> /* memset */
#include <linux/capability.h>
#include <linux/errno.h>
#include <linux/pagemap.h>
#include <linux/uio.h>

#include "udf_i.h"
#include "udf_sb.h"

static vm_fault_t udf_page_mkwrite(struct vm_fault *vmf)
{
	struct vm_area_struct *vma = vmf->vma;
	struct inode *inode = file_inode(vma->vm_file);
	struct address_space *mapping = inode->i_mapping;
	struct page *page = vmf->page;
	loff_t size;
	unsigned int end;
	vm_fault_t ret = VM_FAULT_LOCKED;
	int err;

	sb_start_pagefault(inode->i_sb);
	file_update_time(vma->vm_file);
	filemap_invalidate_lock_shared(mapping);
	lock_page(page);
	size = i_size_read(inode);
	if (page->mapping != inode->i_mapping || page_offset(page) >= size) {
		unlock_page(page);
		ret = VM_FAULT_NOPAGE;
		goto out_unlock;
	}
	/* Space is already allocated for in-ICB file */
	if (UDF_I(inode)->i_alloc_type == ICBTAG_FLAG_AD_IN_ICB)
		goto out_dirty;
	if (page->index == size >> PAGE_SHIFT)
		end = size & ~PAGE_MASK;
	else
		end = PAGE_SIZE;
	err = __block_write_begin(page, 0, end, udf_get_block);
	if (!err)
		err = block_commit_write(page, 0, end);
	if (err < 0) {
		unlock_page(page);
		ret = block_page_mkwrite_return(err);
		goto out_unlock;
	}
out_dirty:
	set_page_dirty(page);
	wait_for_stable_page(page);
out_unlock:
	filemap_invalidate_unlock_shared(mapping);
	sb_end_pagefault(inode->i_sb);
	return ret;
}

static const struct vm_operations_struct udf_file_vm_ops = {
	.fault		= filemap_fault,
	.map_pages	= filemap_map_pages,
	.page_mkwrite	= udf_page_mkwrite,
};

static ssize_t udf_file_write_iter(struct kiocb *iocb, struct iov_iter *from)
{
	ssize_t retval;
	struct file *file = iocb->ki_filp;
	struct inode *inode = file_inode(file);
	struct udf_inode_info *iinfo = UDF_I(inode);

	inode_lock(inode);

	retval = generic_write_checks(iocb, from);
	if (retval <= 0)
		goto out;

	if (iinfo->i_alloc_type == ICBTAG_FLAG_AD_IN_ICB &&
	    inode->i_sb->s_blocksize < (udf_file_entry_alloc_offset(inode) +
				 iocb->ki_pos + iov_iter_count(from))) {
		filemap_invalidate_lock(inode->i_mapping);
		retval = udf_expand_file_adinicb(inode);
		filemap_invalidate_unlock(inode->i_mapping);
		if (retval)
			goto out;
	}

	retval = __generic_file_write_iter(iocb, from);
out:
	if (iinfo->i_alloc_type == ICBTAG_FLAG_AD_IN_ICB && retval > 0) {
		down_write(&iinfo->i_data_sem);
		iinfo->i_lenAlloc = inode->i_size;
		up_write(&iinfo->i_data_sem);
	}
	inode_unlock(inode);

	if (retval > 0) {
		mark_inode_dirty(inode);
		retval = generic_write_sync(iocb, retval);
	}

	return retval;
}

long udf_ioctl(struct file *filp, unsigned int cmd, unsigned long arg)
{
	struct inode *inode = file_inode(filp);
	long old_block, new_block;
	int result;

	if (file_permission(filp, MAY_READ) != 0) {
		udf_debug("no permission to access inode %lu\n", inode->i_ino);
		return -EPERM;
	}

	if (!arg && ((cmd == UDF_GETVOLIDENT) || (cmd == UDF_GETEASIZE) ||
		     (cmd == UDF_RELOCATE_BLOCKS) || (cmd == UDF_GETEABLOCK))) {
		udf_debug("invalid argument to udf_ioctl\n");
		return -EINVAL;
	}

	switch (cmd) {
	case UDF_GETVOLIDENT:
		if (copy_to_user((char __user *)arg,
				 UDF_SB(inode->i_sb)->s_volume_ident, 32))
			return -EFAULT;
		return 0;
	case UDF_RELOCATE_BLOCKS:
		if (!capable(CAP_SYS_ADMIN))
			return -EPERM;
		if (get_user(old_block, (long __user *)arg))
			return -EFAULT;
		result = udf_relocate_blocks(inode->i_sb,
						old_block, &new_block);
		if (result == 0)
			result = put_user(new_block, (long __user *)arg);
		return result;
	case UDF_GETEASIZE:
		return put_user(UDF_I(inode)->i_lenEAttr, (int __user *)arg);
	case UDF_GETEABLOCK:
		return copy_to_user((char __user *)arg,
				    UDF_I(inode)->i_data,
				    UDF_I(inode)->i_lenEAttr) ? -EFAULT : 0;
	default:
		return -ENOIOCTLCMD;
	}

	return 0;
}

static int udf_release_file(struct inode *inode, struct file *filp)
{
	if (filp->f_mode & FMODE_WRITE &&
	    atomic_read(&inode->i_writecount) == 1) {
		/*
		 * Grab i_mutex to avoid races with writes changing i_size
		 * while we are running.
		 */
		inode_lock(inode);
		down_write(&UDF_I(inode)->i_data_sem);
		udf_discard_prealloc(inode);
		udf_truncate_tail_extent(inode);
		up_write(&UDF_I(inode)->i_data_sem);
		inode_unlock(inode);
	}
	return 0;
}

static int udf_file_mmap(struct file *file, struct vm_area_struct *vma)
{
	file_accessed(file);
	vma->vm_ops = &udf_file_vm_ops;

	return 0;
}

const struct file_operations udf_file_operations = {
	.read_iter		= generic_file_read_iter,
	.unlocked_ioctl		= udf_ioctl,
	.open			= generic_file_open,
	.mmap			= udf_file_mmap,
	.write_iter		= udf_file_write_iter,
	.release		= udf_release_file,
	.fsync			= generic_file_fsync,
	.splice_read		= generic_file_splice_read,
	.splice_write		= iter_file_splice_write,
	.llseek			= generic_file_llseek,
};

static int udf_setattr(struct mnt_idmap *idmap, struct dentry *dentry,
		       struct iattr *attr)
{
	struct inode *inode = d_inode(dentry);
	struct super_block *sb = inode->i_sb;
	int error;

	error = setattr_prepare(&nop_mnt_idmap, dentry, attr);
	if (error)
		return error;

	if ((attr->ia_valid & ATTR_UID) &&
	    UDF_QUERY_FLAG(sb, UDF_FLAG_UID_SET) &&
	    !uid_eq(attr->ia_uid, UDF_SB(sb)->s_uid))
		return -EPERM;
	if ((attr->ia_valid & ATTR_GID) &&
	    UDF_QUERY_FLAG(sb, UDF_FLAG_GID_SET) &&
	    !gid_eq(attr->ia_gid, UDF_SB(sb)->s_gid))
		return -EPERM;

	if ((attr->ia_valid & ATTR_SIZE) &&
	    attr->ia_size != i_size_read(inode)) {
		error = udf_setsize(inode, attr->ia_size);
		if (error)
			return error;
	}

	if (attr->ia_valid & ATTR_MODE)
		udf_update_extra_perms(inode, attr->ia_mode);

	setattr_copy(&nop_mnt_idmap, inode, attr);
	mark_inode_dirty(inode);
	return 0;
}

const struct inode_operations udf_file_inode_operations = {
	.setattr		= udf_setattr,
};
