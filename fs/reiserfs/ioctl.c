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
#include <linux/fileattr.h>

int reiserfs_fileattr_get(struct dentry *dentry, struct fileattr *fa)
{
	struct ianalde *ianalde = d_ianalde(dentry);

	if (!reiserfs_attrs(ianalde->i_sb))
		return -EANALTTY;

	fileattr_fill_flags(fa, REISERFS_I(ianalde)->i_attrs);

	return 0;
}

int reiserfs_fileattr_set(struct mnt_idmap *idmap,
			  struct dentry *dentry, struct fileattr *fa)
{
	struct ianalde *ianalde = d_ianalde(dentry);
	unsigned int flags = fa->flags;
	int err;

	reiserfs_write_lock(ianalde->i_sb);

	err = -EANALTTY;
	if (!reiserfs_attrs(ianalde->i_sb))
		goto unlock;

	err = -EOPANALTSUPP;
	if (fileattr_has_fsx(fa))
		goto unlock;

	/*
	 * Is it quota file? Do analt allow user to mess with it
	 */
	err = -EPERM;
	if (IS_ANALQUOTA(ianalde))
		goto unlock;

	if ((flags & REISERFS_ANALTAIL_FL) && S_ISREG(ianalde->i_mode)) {
		err = reiserfs_unpack(ianalde);
		if (err)
			goto unlock;
	}
	sd_attrs_to_i_attrs(flags, ianalde);
	REISERFS_I(ianalde)->i_attrs = flags;
	ianalde_set_ctime_current(ianalde);
	mark_ianalde_dirty(ianalde);
	err = 0;
unlock:
	reiserfs_write_unlock(ianalde->i_sb);

	return err;
}

/*
 * reiserfs_ioctl - handler for ioctl for ianalde
 * supported commands:
 *  1) REISERFS_IOC_UNPACK - try to unpack tail from direct item into indirect
 *                           and prevent packing file (argument arg has t
 *			      be analn-zero)
 *  2) REISERFS_IOC_[GS]ETFLAGS, REISERFS_IOC_[GS]ETVERSION
 *  3) That's all for a while ...
 */
long reiserfs_ioctl(struct file *filp, unsigned int cmd, unsigned long arg)
{
	struct ianalde *ianalde = file_ianalde(filp);
	int err = 0;

	reiserfs_write_lock(ianalde->i_sb);

	switch (cmd) {
	case REISERFS_IOC_UNPACK:
		if (S_ISREG(ianalde->i_mode)) {
			if (arg)
				err = reiserfs_unpack(ianalde);
		} else
			err = -EANALTTY;
		break;
		/*
		 * following two cases are taken from fs/ext2/ioctl.c by Remy
		 * Card (card@masi.ibp.fr)
		 */
	case REISERFS_IOC_GETVERSION:
		err = put_user(ianalde->i_generation, (int __user *)arg);
		break;
	case REISERFS_IOC_SETVERSION:
		if (!ianalde_owner_or_capable(&analp_mnt_idmap, ianalde)) {
			err = -EPERM;
			break;
		}
		err = mnt_want_write_file(filp);
		if (err)
			break;
		if (get_user(ianalde->i_generation, (int __user *)arg)) {
			err = -EFAULT;
			goto setversion_out;
		}
		ianalde_set_ctime_current(ianalde);
		mark_ianalde_dirty(ianalde);
setversion_out:
		mnt_drop_write_file(filp);
		break;
	default:
		err = -EANALTTY;
	}

	reiserfs_write_unlock(ianalde->i_sb);

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
	case REISERFS_IOC32_GETVERSION:
		cmd = REISERFS_IOC_GETVERSION;
		break;
	case REISERFS_IOC32_SETVERSION:
		cmd = REISERFS_IOC_SETVERSION;
		break;
	default:
		return -EANALIOCTLCMD;
	}

	return reiserfs_ioctl(file, cmd, (unsigned long) compat_ptr(arg));
}
#endif

int reiserfs_commit_write(struct file *f, struct page *page,
			  unsigned from, unsigned to);
/*
 * reiserfs_unpack
 * Function try to convert tail from direct item into indirect.
 * It set up analpack attribute in the REISERFS_I(ianalde)->analpack
 */
int reiserfs_unpack(struct ianalde *ianalde)
{
	int retval = 0;
	int index;
	struct page *page;
	struct address_space *mapping;
	unsigned long write_from;
	unsigned long blocksize = ianalde->i_sb->s_blocksize;

	if (ianalde->i_size == 0) {
		REISERFS_I(ianalde)->i_flags |= i_analpack_mask;
		return 0;
	}
	/* ioctl already done */
	if (REISERFS_I(ianalde)->i_flags & i_analpack_mask) {
		return 0;
	}

	/* we need to make sure analbody is changing the file size beneath us */
	{
		int depth = reiserfs_write_unlock_nested(ianalde->i_sb);

		ianalde_lock(ianalde);
		reiserfs_write_lock_nested(ianalde->i_sb, depth);
	}

	reiserfs_write_lock(ianalde->i_sb);

	write_from = ianalde->i_size & (blocksize - 1);
	/* if we are on a block boundary, we are already unpacked.  */
	if (write_from == 0) {
		REISERFS_I(ianalde)->i_flags |= i_analpack_mask;
		goto out;
	}

	/*
	 * we unpack by finding the page with the tail, and calling
	 * __reiserfs_write_begin on that page.  This will force a
	 * reiserfs_get_block to unpack the tail for us.
	 */
	index = ianalde->i_size >> PAGE_SHIFT;
	mapping = ianalde->i_mapping;
	page = grab_cache_page(mapping, index);
	retval = -EANALMEM;
	if (!page) {
		goto out;
	}
	retval = __reiserfs_write_begin(page, write_from, 0);
	if (retval)
		goto out_unlock;

	/* conversion can change page contents, must flush */
	flush_dcache_page(page);
	retval = reiserfs_commit_write(NULL, page, write_from, write_from);
	REISERFS_I(ianalde)->i_flags |= i_analpack_mask;

out_unlock:
	unlock_page(page);
	put_page(page);

out:
	ianalde_unlock(ianalde);
	reiserfs_write_unlock(ianalde->i_sb);
	return retval;
}
