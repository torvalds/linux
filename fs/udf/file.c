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
#include <linux/udf_fs.h>
#include <asm/uaccess.h>
#include <linux/kernel.h>
#include <linux/string.h> /* memset */
#include <linux/capability.h>
#include <linux/errno.h>
#include <linux/smp_lock.h>
#include <linux/pagemap.h>
#include <linux/buffer_head.h>
#include <linux/aio.h>

#include "udf_i.h"
#include "udf_sb.h"

static int udf_adinicb_readpage(struct file *file, struct page *page)
{
	struct inode *inode = page->mapping->host;
	char *kaddr;

	BUG_ON(!PageLocked(page));

	kaddr = kmap(page);
	memset(kaddr, 0, PAGE_CACHE_SIZE);
	memcpy(kaddr, UDF_I_DATA(inode) + UDF_I_LENEATTR(inode), inode->i_size);
	flush_dcache_page(page);
	SetPageUptodate(page);
	kunmap(page);
	unlock_page(page);

	return 0;
}

static int udf_adinicb_writepage(struct page *page, struct writeback_control *wbc)
{
	struct inode *inode = page->mapping->host;
	char *kaddr;

	BUG_ON(!PageLocked(page));

	kaddr = kmap(page);
	memcpy(UDF_I_DATA(inode) + UDF_I_LENEATTR(inode), kaddr, inode->i_size);
	mark_inode_dirty(inode);
	SetPageUptodate(page);
	kunmap(page);
	unlock_page(page);

	return 0;
}

static int udf_adinicb_write_end(struct file *file,
			struct address_space *mapping,
			loff_t pos, unsigned len, unsigned copied,
			struct page *page, void *fsdata)
{
	struct inode *inode = mapping->host;
	unsigned offset = pos & (PAGE_CACHE_SIZE - 1);
	char *kaddr;

	kaddr = kmap_atomic(page, KM_USER0);
	memcpy(UDF_I_DATA(inode) + UDF_I_LENEATTR(inode) + offset,
		kaddr + offset, copied);
	kunmap_atomic(kaddr, KM_USER0);

	return simple_write_end(file, mapping, pos, len, copied, page, fsdata);
}

const struct address_space_operations udf_adinicb_aops = {
	.readpage	= udf_adinicb_readpage,
	.writepage	= udf_adinicb_writepage,
	.sync_page	= block_sync_page,
	.write_begin = simple_write_begin,
	.write_end = udf_adinicb_write_end,
};

static ssize_t udf_file_aio_write(struct kiocb *iocb, const struct iovec *iov,
				  unsigned long nr_segs, loff_t ppos)
{
	ssize_t retval;
	struct file *file = iocb->ki_filp;
	struct inode *inode = file->f_path.dentry->d_inode;
	int err, pos;
	size_t count = iocb->ki_left;

	if (UDF_I_ALLOCTYPE(inode) == ICBTAG_FLAG_AD_IN_ICB) {
		if (file->f_flags & O_APPEND)
			pos = inode->i_size;
		else
			pos = ppos;

		if (inode->i_sb->s_blocksize < (udf_file_entry_alloc_offset(inode) +
						pos + count)) {
			udf_expand_file_adinicb(inode, pos + count, &err);
			if (UDF_I_ALLOCTYPE(inode) == ICBTAG_FLAG_AD_IN_ICB) {
				udf_debug("udf_expand_adinicb: err=%d\n", err);
				return err;
			}
		} else {
			if (pos + count > inode->i_size)
				UDF_I_LENALLOC(inode) = pos + count;
			else
				UDF_I_LENALLOC(inode) = inode->i_size;
		}
	}

	retval = generic_file_aio_write(iocb, iov, nr_segs, ppos);
	if (retval > 0)
		mark_inode_dirty(inode);

	return retval;
}

/*
 * udf_ioctl
 *
 * PURPOSE
 *	Issue an ioctl.
 *
 * DESCRIPTION
 *	Optional - sys_ioctl() will return -ENOTTY if this routine is not
 *	available, and the ioctl cannot be handled without filesystem help.
 *
 *	sys_ioctl() handles these ioctls that apply only to regular files:
 *		FIBMAP [requires udf_block_map()], FIGETBSZ, FIONREAD
 *	These ioctls are also handled by sys_ioctl():
 *		FIOCLEX, FIONCLEX, FIONBIO, FIOASYNC
 *	All other ioctls are passed to the filesystem.
 *
 *	Refer to sys_ioctl() in fs/ioctl.c
 *	sys_ioctl() -> .
 *
 * PRE-CONDITIONS
 *	inode			Pointer to inode that ioctl was issued on.
 *	filp			Pointer to file that ioctl was issued on.
 *	cmd			The ioctl command.
 *	arg			The ioctl argument [can be interpreted as a
 *				user-space pointer if desired].
 *
 * POST-CONDITIONS
 *	<return>		Success (>=0) or an error code (<=0) that
 *				sys_ioctl() will return.
 *
 * HISTORY
 *	July 1, 1997 - Andrew E. Mileski
 *	Written, tested, and released.
 */
int udf_ioctl(struct inode *inode, struct file *filp, unsigned int cmd,
	      unsigned long arg)
{
	long old_block, new_block;
	int result = -EINVAL;

	if (file_permission(filp, MAY_READ) != 0) {
		udf_debug("no permission to access inode %lu\n",
			  inode->i_ino);
		return -EPERM;
	}

	if (!arg) {
		udf_debug("invalid argument to udf_ioctl\n");
		return -EINVAL;
	}

	switch (cmd) {
	case UDF_GETVOLIDENT:
		return copy_to_user((char __user *)arg,
				    UDF_SB_VOLIDENT(inode->i_sb), 32) ? -EFAULT : 0;
	case UDF_RELOCATE_BLOCKS:
		if (!capable(CAP_SYS_ADMIN))
			return -EACCES;
		if (get_user(old_block, (long __user *)arg))
			return -EFAULT;
		if ((result = udf_relocate_blocks(inode->i_sb,
						  old_block, &new_block)) == 0)
			result = put_user(new_block, (long __user *)arg);
		return result;
	case UDF_GETEASIZE:
		result = put_user(UDF_I_LENEATTR(inode), (int __user *)arg);
		break;
	case UDF_GETEABLOCK:
		result = copy_to_user((char __user *)arg, UDF_I_DATA(inode),
				      UDF_I_LENEATTR(inode)) ? -EFAULT : 0;
		break;
	}

	return result;
}

/*
 * udf_release_file
 *
 * PURPOSE
 *  Called when all references to the file are closed
 *
 * DESCRIPTION
 *  Discard prealloced blocks
 *
 * HISTORY
 *
 */
static int udf_release_file(struct inode *inode, struct file *filp)
{
	if (filp->f_mode & FMODE_WRITE) {
		lock_kernel();
		udf_discard_prealloc(inode);
		unlock_kernel();
	}
	return 0;
}

const struct file_operations udf_file_operations = {
	.read			= do_sync_read,
	.aio_read		= generic_file_aio_read,
	.ioctl			= udf_ioctl,
	.open			= generic_file_open,
	.mmap			= generic_file_mmap,
	.write			= do_sync_write,
	.aio_write		= udf_file_aio_write,
	.release		= udf_release_file,
	.fsync			= udf_fsync_file,
	.splice_read		= generic_file_splice_read,
};

const struct inode_operations udf_file_inode_operations = {
	.truncate = udf_truncate,
};
