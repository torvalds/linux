/*
 * Copyright 2000 by Hans Reiser, licensing governed by reiserfs/README
 */

#include <linux/capability.h>
#include <linux/fs.h>
#include <linux/mount.h>
#include <linux/reiserfs_fs.h>
#include <linux/time.h>
#include <asm/uaccess.h>
#include <linux/pagemap.h>
#include <linux/smp_lock.h>
#include <linux/compat.h>

/*
 * reiserfs_ioctl - handler for ioctl for inode
 * supported commands:
 *  1) REISERFS_IOC_UNPACK - try to unpack tail from direct item into indirect
 *                           and prevent packing file (argument arg has to be non-zero)
 *  2) REISERFS_IOC_[GS]ETFLAGS, REISERFS_IOC_[GS]ETVERSION
 *  3) That's all for a while ...
 */
long reiserfs_ioctl(struct file *filp, unsigned int cmd, unsigned long arg)
{
	struct inode *inode = filp->f_path.dentry->d_inode;
	unsigned int flags;
	int err = 0;

	reiserfs_write_lock(inode->i_sb);

	switch (cmd) {
	case REISERFS_IOC_UNPACK:
		if (S_ISREG(inode->i_mode)) {
			if (arg)
				err = reiserfs_unpack(inode, filp);
		} else
			err = -ENOTTY;
		break;
		/*
		 * following two cases are taken from fs/ext2/ioctl.c by Remy
		 * Card (card@masi.ibp.fr)
		 */
	case REISERFS_IOC_GETFLAGS:
		if (!reiserfs_attrs(inode->i_sb)) {
			err = -ENOTTY;
			break;
		}

		flags = REISERFS_I(inode)->i_attrs;
		i_attrs_to_sd_attrs(inode, (__u16 *) & flags);
		err = put_user(flags, (int __user *)arg);
		break;
	case REISERFS_IOC_SETFLAGS:{
			if (!reiserfs_attrs(inode->i_sb)) {
				err = -ENOTTY;
				break;
			}

			err = mnt_want_write(filp->f_path.mnt);
			if (err)
				break;

			if (!is_owner_or_cap(inode)) {
				err = -EPERM;
				goto setflags_out;
			}
			if (get_user(flags, (int __user *)arg)) {
				err = -EFAULT;
				goto setflags_out;
			}
			/*
			 * Is it quota file? Do not allow user to mess with it
			 */
			if (IS_NOQUOTA(inode)) {
				err = -EPERM;
				goto setflags_out;
			}
			if (((flags ^ REISERFS_I(inode)->
			      i_attrs) & (REISERFS_IMMUTABLE_FL |
					  REISERFS_APPEND_FL))
			    && !capable(CAP_LINUX_IMMUTABLE)) {
				err = -EPERM;
				goto setflags_out;
			}
			if ((flags & REISERFS_NOTAIL_FL) &&
			    S_ISREG(inode->i_mode)) {
				int result;

				result = reiserfs_unpack(inode, filp);
				if (result) {
					err = result;
					goto setflags_out;
				}
			}
			sd_attrs_to_i_attrs(flags, inode);
			REISERFS_I(inode)->i_attrs = flags;
			inode->i_ctime = CURRENT_TIME_SEC;
			mark_inode_dirty(inode);
setflags_out:
			mnt_drop_write(filp->f_path.mnt);
			break;
		}
	case REISERFS_IOC_GETVERSION:
		err = put_user(inode->i_generation, (int __user *)arg);
		break;
	case REISERFS_IOC_SETVERSION:
		if (!is_owner_or_cap(inode)) {
			err = -EPERM;
			break;
		}
		err = mnt_want_write(filp->f_path.mnt);
		if (err)
			break;
		if (get_user(inode->i_generation, (int __user *)arg)) {
			err = -EFAULT;
			goto setversion_out;
		}
		inode->i_ctime = CURRENT_TIME_SEC;
		mark_inode_dirty(inode);
setversion_out:
		mnt_drop_write(filp->f_path.mnt);
		break;
	default:
		err = -ENOTTY;
	}

	reiserfs_write_unlock(inode->i_sb);

	return err;
}

#ifdef CONFIG_COMPAT
long reiserfs_compat_ioctl(struct file *file, unsigned int cmd,
				unsigned long arg)
{
	/* These are just misnamed, they actually get/put from/to user an int */
	switch (cmd) {
	case REISERFS_IOC32_UNPACK:
		cmd = REISERFS_IOC_UNPACK;
		break;
	case REISERFS_IOC32_GETFLAGS:
		cmd = REISERFS_IOC_GETFLAGS;
		break;
	case REISERFS_IOC32_SETFLAGS:
		cmd = REISERFS_IOC_SETFLAGS;
		break;
	case REISERFS_IOC32_GETVERSION:
		cmd = REISERFS_IOC_GETVERSION;
		break;
	case REISERFS_IOC32_SETVERSION:
		cmd = REISERFS_IOC_SETVERSION;
		break;
	default:
		return -ENOIOCTLCMD;
	}

	return reiserfs_ioctl(file, cmd, (unsigned long) compat_ptr(arg));
}
#endif

int reiserfs_commit_write(struct file *f, struct page *page,
			  unsigned from, unsigned to);
int reiserfs_prepare_write(struct file *f, struct page *page,
			   unsigned from, unsigned to);
/*
** reiserfs_unpack
** Function try to convert tail from direct item into indirect.
** It set up nopack attribute in the REISERFS_I(inode)->nopack
*/
int reiserfs_unpack(struct inode *inode, struct file *filp)
{
	int retval = 0;
	int depth;
	int index;
	struct page *page;
	struct address_space *mapping;
	unsigned long write_from;
	unsigned long blocksize = inode->i_sb->s_blocksize;

	if (inode->i_size == 0) {
		REISERFS_I(inode)->i_flags |= i_nopack_mask;
		return 0;
	}
	/* ioctl already done */
	if (REISERFS_I(inode)->i_flags & i_nopack_mask) {
		return 0;
	}

	depth = reiserfs_write_lock_once(inode->i_sb);

	/* we need to make sure nobody is changing the file size beneath us */
	reiserfs_mutex_lock_safe(&inode->i_mutex, inode->i_sb);

	write_from = inode->i_size & (blocksize - 1);
	/* if we are on a block boundary, we are already unpacked.  */
	if (write_from == 0) {
		REISERFS_I(inode)->i_flags |= i_nopack_mask;
		goto out;
	}

	/* we unpack by finding the page with the tail, and calling
	 ** reiserfs_prepare_write on that page.  This will force a
	 ** reiserfs_get_block to unpack the tail for us.
	 */
	index = inode->i_size >> PAGE_CACHE_SHIFT;
	mapping = inode->i_mapping;
	page = grab_cache_page(mapping, index);
	retval = -ENOMEM;
	if (!page) {
		goto out;
	}
	retval = reiserfs_prepare_write(NULL, page, write_from, write_from);
	if (retval)
		goto out_unlock;

	/* conversion can change page contents, must flush */
	flush_dcache_page(page);
	retval = reiserfs_commit_write(NULL, page, write_from, write_from);
	REISERFS_I(inode)->i_flags |= i_nopack_mask;

      out_unlock:
	unlock_page(page);
	page_cache_release(page);

      out:
	mutex_unlock(&inode->i_mutex);
	reiserfs_write_unlock_once(inode->i_sb, depth);
	return retval;
}
