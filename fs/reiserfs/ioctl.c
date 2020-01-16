/*
 * Copyright 2000 by Hans Reiser, licensing governed by reiserfs/README
 */

#include <linux/capability.h>
#include <linux/fs.h>
#include <linux/mount.h>
#include "reiserfs.h"
#include <linux/time.h>
#include <linux/uaccess.h>
#include <linux/pagemap.h>
#include <linux/compat.h>

/*
 * reiserfs_ioctl - handler for ioctl for iyesde
 * supported commands:
 *  1) REISERFS_IOC_UNPACK - try to unpack tail from direct item into indirect
 *                           and prevent packing file (argument arg has t
 *			      be yesn-zero)
 *  2) REISERFS_IOC_[GS]ETFLAGS, REISERFS_IOC_[GS]ETVERSION
 *  3) That's all for a while ...
 */
long reiserfs_ioctl(struct file *filp, unsigned int cmd, unsigned long arg)
{
	struct iyesde *iyesde = file_iyesde(filp);
	unsigned int flags;
	int err = 0;

	reiserfs_write_lock(iyesde->i_sb);

	switch (cmd) {
	case REISERFS_IOC_UNPACK:
		if (S_ISREG(iyesde->i_mode)) {
			if (arg)
				err = reiserfs_unpack(iyesde, filp);
		} else
			err = -ENOTTY;
		break;
		/*
		 * following two cases are taken from fs/ext2/ioctl.c by Remy
		 * Card (card@masi.ibp.fr)
		 */
	case REISERFS_IOC_GETFLAGS:
		if (!reiserfs_attrs(iyesde->i_sb)) {
			err = -ENOTTY;
			break;
		}

		flags = REISERFS_I(iyesde)->i_attrs;
		err = put_user(flags, (int __user *)arg);
		break;
	case REISERFS_IOC_SETFLAGS:{
			if (!reiserfs_attrs(iyesde->i_sb)) {
				err = -ENOTTY;
				break;
			}

			err = mnt_want_write_file(filp);
			if (err)
				break;

			if (!iyesde_owner_or_capable(iyesde)) {
				err = -EPERM;
				goto setflags_out;
			}
			if (get_user(flags, (int __user *)arg)) {
				err = -EFAULT;
				goto setflags_out;
			}
			/*
			 * Is it quota file? Do yest allow user to mess with it
			 */
			if (IS_NOQUOTA(iyesde)) {
				err = -EPERM;
				goto setflags_out;
			}
			err = vfs_ioc_setflags_prepare(iyesde,
						     REISERFS_I(iyesde)->i_attrs,
						     flags);
			if (err)
				goto setflags_out;
			if ((flags & REISERFS_NOTAIL_FL) &&
			    S_ISREG(iyesde->i_mode)) {
				int result;

				result = reiserfs_unpack(iyesde, filp);
				if (result) {
					err = result;
					goto setflags_out;
				}
			}
			sd_attrs_to_i_attrs(flags, iyesde);
			REISERFS_I(iyesde)->i_attrs = flags;
			iyesde->i_ctime = current_time(iyesde);
			mark_iyesde_dirty(iyesde);
setflags_out:
			mnt_drop_write_file(filp);
			break;
		}
	case REISERFS_IOC_GETVERSION:
		err = put_user(iyesde->i_generation, (int __user *)arg);
		break;
	case REISERFS_IOC_SETVERSION:
		if (!iyesde_owner_or_capable(iyesde)) {
			err = -EPERM;
			break;
		}
		err = mnt_want_write_file(filp);
		if (err)
			break;
		if (get_user(iyesde->i_generation, (int __user *)arg)) {
			err = -EFAULT;
			goto setversion_out;
		}
		iyesde->i_ctime = current_time(iyesde);
		mark_iyesde_dirty(iyesde);
setversion_out:
		mnt_drop_write_file(filp);
		break;
	default:
		err = -ENOTTY;
	}

	reiserfs_write_unlock(iyesde->i_sb);

	return err;
}

#ifdef CONFIG_COMPAT
long reiserfs_compat_ioctl(struct file *file, unsigned int cmd,
				unsigned long arg)
{
	/*
	 * These are just misnamed, they actually
	 * get/put from/to user an int
	 */
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
/*
 * reiserfs_unpack
 * Function try to convert tail from direct item into indirect.
 * It set up yespack attribute in the REISERFS_I(iyesde)->yespack
 */
int reiserfs_unpack(struct iyesde *iyesde, struct file *filp)
{
	int retval = 0;
	int index;
	struct page *page;
	struct address_space *mapping;
	unsigned long write_from;
	unsigned long blocksize = iyesde->i_sb->s_blocksize;

	if (iyesde->i_size == 0) {
		REISERFS_I(iyesde)->i_flags |= i_yespack_mask;
		return 0;
	}
	/* ioctl already done */
	if (REISERFS_I(iyesde)->i_flags & i_yespack_mask) {
		return 0;
	}

	/* we need to make sure yesbody is changing the file size beneath us */
{
	int depth = reiserfs_write_unlock_nested(iyesde->i_sb);
	iyesde_lock(iyesde);
	reiserfs_write_lock_nested(iyesde->i_sb, depth);
}

	reiserfs_write_lock(iyesde->i_sb);

	write_from = iyesde->i_size & (blocksize - 1);
	/* if we are on a block boundary, we are already unpacked.  */
	if (write_from == 0) {
		REISERFS_I(iyesde)->i_flags |= i_yespack_mask;
		goto out;
	}

	/*
	 * we unpack by finding the page with the tail, and calling
	 * __reiserfs_write_begin on that page.  This will force a
	 * reiserfs_get_block to unpack the tail for us.
	 */
	index = iyesde->i_size >> PAGE_SHIFT;
	mapping = iyesde->i_mapping;
	page = grab_cache_page(mapping, index);
	retval = -ENOMEM;
	if (!page) {
		goto out;
	}
	retval = __reiserfs_write_begin(page, write_from, 0);
	if (retval)
		goto out_unlock;

	/* conversion can change page contents, must flush */
	flush_dcache_page(page);
	retval = reiserfs_commit_write(NULL, page, write_from, write_from);
	REISERFS_I(iyesde)->i_flags |= i_yespack_mask;

out_unlock:
	unlock_page(page);
	put_page(page);

out:
	iyesde_unlock(iyesde);
	reiserfs_write_unlock(iyesde->i_sb);
	return retval;
}
