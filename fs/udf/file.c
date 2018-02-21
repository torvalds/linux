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

static void __udf_adinicb_readpage(struct page *page)
{
	struct inode *inode = page->mapping->host;
	char *kaddr;
	struct udf_inode_info *iinfo = UDF_I(inode);
	loff_t isize = i_size_read(inode);

	/*
	 * We have to be careful here as truncate can change i_size under us.
	 * So just sample it once and use the same value everywhere.
	 */
	kaddr = kmap_atomic(page);
	memcpy(kaddr, iinfo->i_ext.i_data + iinfo->i_lenEAttr, isize);
	memset(kaddr + isize, 0, PAGE_SIZE - isize);
	flush_dcache_page(page);
	SetPageUptodate(page);
	kunmap_atomic(kaddr);
}

static int udf_adinicb_readpage(struct file *file, struct page *page)
{
	BUG_ON(!PageLocked(page));
	__udf_adinicb_readpage(page);
	unlock_page(page);

	return 0;
}

static int udf_adinicb_writepage(struct page *page,
				 struct writeback_control *wbc)
{
	struct inode *inode = page->mapping->host;
	char *kaddr;
	struct udf_inode_info *iinfo = UDF_I(inode);

	BUG_ON(!PageLocked(page));

	kaddr = kmap_atomic(page);
	memcpy(iinfo->i_ext.i_data + iinfo->i_lenEAttr, kaddr,
		i_size_read(inode));
	SetPageUptodate(page);
	kunmap_atomic(kaddr);
	mark_inode_dirty(inode);
	unlock_page(page);

	return 0;
}

static int udf_adinicb_write_begin(struct file *file,
			struct address_space *mapping, loff_t pos,
			unsigned len, unsigned flags, struct page **pagep,
			void **fsdata)
{
	struct page *page;

	if (WARN_ON_ONCE(pos >= PAGE_SIZE))
		return -EIO;
	page = grab_cache_page_write_begin(mapping, 0, flags);
	if (!page)
		return -ENOMEM;
	*pagep = page;

	if (!PageUptodate(page))
		__udf_adinicb_readpage(page);
	return 0;
}

static ssize_t udf_adinicb_direct_IO(struct kiocb *iocb, struct iov_iter *iter)
{
	/* Fallback to buffered I/O. */
	return 0;
}

static int udf_adinicb_write_end(struct file *file, struct address_space *mapping,
				 loff_t pos, unsigned len, unsigned copied,
				 struct page *page, void *fsdata)
{
	struct inode *inode = page->mapping->host;
	loff_t last_pos = pos + copied;
	if (last_pos > inode->i_size)
		i_size_write(inode, last_pos);
	set_page_dirty(page);
	unlock_page(page);
	put_page(page);
	return copied;
}

const struct address_space_operations udf_adinicb_aops = {
	.readpage	= udf_adinicb_readpage,
	.writepage	= udf_adinicb_writepage,
	.write_begin	= udf_adinicb_write_begin,
	.write_end	= udf_adinicb_write_end,
	.direct_IO	= udf_adinicb_direct_IO,
};

static ssize_t udf_file_write_iter(struct kiocb *iocb, struct iov_iter *from)
{
	ssize_t retval;
	struct file *file = iocb->ki_filp;
	struct inode *inode = file_inode(file);
	struct udf_inode_info *iinfo = UDF_I(inode);
	int err;

	inode_lock(inode);

	retval = generic_write_checks(iocb, from);
	if (retval <= 0)
		goto out;

	down_write(&iinfo->i_data_sem);
	if (iinfo->i_alloc_type == ICBTAG_FLAG_AD_IN_ICB) {
		loff_t end = iocb->ki_pos + iov_iter_count(from);

		if (inode->i_sb->s_blocksize <
				(udf_file_entry_alloc_offset(inode) + end)) {
			err = udf_expand_file_adinicb(inode);
			if (err) {
				inode_unlock(inode);
				udf_debug("udf_expand_adinicb: err=%d\n", err);
				return err;
			}
		} else {
			iinfo->i_lenAlloc = max(end, inode->i_size);
			up_write(&iinfo->i_data_sem);
		}
	} else
		up_write(&iinfo->i_data_sem);

	retval = __generic_file_write_iter(iocb, from);
out:
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

	if (inode_permission(inode, MAY_READ) != 0) {
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
				    UDF_I(inode)->i_ext.i_data,
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

const struct file_operations udf_file_operations = {
	.read_iter		= generic_file_read_iter,
	.unlocked_ioctl		= udf_ioctl,
	.open			= generic_file_open,
	.mmap			= generic_file_mmap,
	.write_iter		= udf_file_write_iter,
	.release		= udf_release_file,
	.fsync			= generic_file_fsync,
	.splice_read		= generic_file_splice_read,
	.llseek			= generic_file_llseek,
};

static int udf_setattr(struct dentry *dentry, struct iattr *attr)
{
	struct inode *inode = d_inode(dentry);
	struct super_block *sb = inode->i_sb;
	int error;

	error = setattr_prepare(dentry, attr);
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

	setattr_copy(inode, attr);
	mark_inode_dirty(inode);
	return 0;
}

const struct inode_operations udf_file_inode_operations = {
	.setattr		= udf_setattr,
};
