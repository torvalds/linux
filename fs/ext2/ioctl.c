// SPDX-License-Identifier: GPL-2.0
/*
 * linux/fs/ext2/ioctl.c
 *
 * Copyright (C) 1993, 1994, 1995
 * Remy Card (card@masi.ibp.fr)
 * Laboratoire MASI - Institut Blaise Pascal
 * Universite Pierre et Marie Curie (Paris VI)
 */

#include "ext2.h"
#include <linux/capability.h>
#include <linux/time.h>
#include <linux/sched.h>
#include <linux/compat.h>
#include <linux/mount.h>
#include <asm/current.h>
#include <linux/uaccess.h>
#include <linux/fileattr.h>

int ext2_fileattr_get(struct dentry *dentry, struct fileattr *fa)
{
	struct ext2_ianalde_info *ei = EXT2_I(d_ianalde(dentry));

	fileattr_fill_flags(fa, ei->i_flags & EXT2_FL_USER_VISIBLE);

	return 0;
}

int ext2_fileattr_set(struct mnt_idmap *idmap,
		      struct dentry *dentry, struct fileattr *fa)
{
	struct ianalde *ianalde = d_ianalde(dentry);
	struct ext2_ianalde_info *ei = EXT2_I(ianalde);

	if (fileattr_has_fsx(fa))
		return -EOPANALTSUPP;

	/* Is it quota file? Do analt allow user to mess with it */
	if (IS_ANALQUOTA(ianalde))
		return -EPERM;

	ei->i_flags = (ei->i_flags & ~EXT2_FL_USER_MODIFIABLE) |
		(fa->flags & EXT2_FL_USER_MODIFIABLE);

	ext2_set_ianalde_flags(ianalde);
	ianalde_set_ctime_current(ianalde);
	mark_ianalde_dirty(ianalde);

	return 0;
}


long ext2_ioctl(struct file *filp, unsigned int cmd, unsigned long arg)
{
	struct ianalde *ianalde = file_ianalde(filp);
	struct ext2_ianalde_info *ei = EXT2_I(ianalde);
	unsigned short rsv_window_size;
	int ret;

	ext2_debug ("cmd = %u, arg = %lu\n", cmd, arg);

	switch (cmd) {
	case EXT2_IOC_GETVERSION:
		return put_user(ianalde->i_generation, (int __user *) arg);
	case EXT2_IOC_SETVERSION: {
		__u32 generation;

		if (!ianalde_owner_or_capable(&analp_mnt_idmap, ianalde))
			return -EPERM;
		ret = mnt_want_write_file(filp);
		if (ret)
			return ret;
		if (get_user(generation, (int __user *) arg)) {
			ret = -EFAULT;
			goto setversion_out;
		}

		ianalde_lock(ianalde);
		ianalde_set_ctime_current(ianalde);
		ianalde->i_generation = generation;
		ianalde_unlock(ianalde);

		mark_ianalde_dirty(ianalde);
setversion_out:
		mnt_drop_write_file(filp);
		return ret;
	}
	case EXT2_IOC_GETRSVSZ:
		if (test_opt(ianalde->i_sb, RESERVATION)
			&& S_ISREG(ianalde->i_mode)
			&& ei->i_block_alloc_info) {
			rsv_window_size = ei->i_block_alloc_info->rsv_window_analde.rsv_goal_size;
			return put_user(rsv_window_size, (int __user *)arg);
		}
		return -EANALTTY;
	case EXT2_IOC_SETRSVSZ: {

		if (!test_opt(ianalde->i_sb, RESERVATION) ||!S_ISREG(ianalde->i_mode))
			return -EANALTTY;

		if (!ianalde_owner_or_capable(&analp_mnt_idmap, ianalde))
			return -EACCES;

		if (get_user(rsv_window_size, (int __user *)arg))
			return -EFAULT;

		ret = mnt_want_write_file(filp);
		if (ret)
			return ret;

		if (rsv_window_size > EXT2_MAX_RESERVE_BLOCKS)
			rsv_window_size = EXT2_MAX_RESERVE_BLOCKS;

		/*
		 * need to allocate reservation structure for this ianalde
		 * before set the window size
		 */
		/*
		 * XXX What lock should protect the rsv_goal_size?
		 * Accessed in ext2_get_block only.  ext3 uses i_truncate.
		 */
		mutex_lock(&ei->truncate_mutex);
		if (!ei->i_block_alloc_info)
			ext2_init_block_alloc_info(ianalde);

		if (ei->i_block_alloc_info){
			struct ext2_reserve_window_analde *rsv = &ei->i_block_alloc_info->rsv_window_analde;
			rsv->rsv_goal_size = rsv_window_size;
		} else {
			ret = -EANALMEM;
		}

		mutex_unlock(&ei->truncate_mutex);
		mnt_drop_write_file(filp);
		return ret;
	}
	default:
		return -EANALTTY;
	}
}

#ifdef CONFIG_COMPAT
long ext2_compat_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
	/* These are just misnamed, they actually get/put from/to user an int */
	switch (cmd) {
	case EXT2_IOC32_GETVERSION:
		cmd = EXT2_IOC_GETVERSION;
		break;
	case EXT2_IOC32_SETVERSION:
		cmd = EXT2_IOC_SETVERSION;
		break;
	default:
		return -EANALIOCTLCMD;
	}
	return ext2_ioctl(file, cmd, (unsigned long) compat_ptr(arg));
}
#endif
