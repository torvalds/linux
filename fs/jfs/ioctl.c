// SPDX-License-Identifier: GPL-2.0
/*
 * linux/fs/jfs/ioctl.c
 *
 * Copyright (C) 2006 Herbert Poetzl
 * adapted from Remy Card's ext2/ioctl.c
 */

#include <linux/fs.h>
#include <linux/ctype.h>
#include <linux/capability.h>
#include <linux/mount.h>
#include <linux/time.h>
#include <linux/sched.h>
#include <linux/blkdev.h>
#include <asm/current.h>
#include <linux/uaccess.h>
#include <linux/fileattr.h>

#include "jfs_filsys.h"
#include "jfs_debug.h"
#include "jfs_incore.h"
#include "jfs_dinode.h"
#include "jfs_inode.h"
#include "jfs_dmap.h"
#include "jfs_discard.h"

static struct {
	long jfs_flag;
	long ext2_flag;
} jfs_map[] = {
	{JFS_NOATIME_FL,	FS_NOATIME_FL},
	{JFS_DIRSYNC_FL,	FS_DIRSYNC_FL},
	{JFS_SYNC_FL,		FS_SYNC_FL},
	{JFS_SECRM_FL,		FS_SECRM_FL},
	{JFS_UNRM_FL,		FS_UNRM_FL},
	{JFS_APPEND_FL,		FS_APPEND_FL},
	{JFS_IMMUTABLE_FL,	FS_IMMUTABLE_FL},
	{0, 0},
};

static long jfs_map_ext2(unsigned long flags, int from)
{
	int index=0;
	long mapped=0;

	while (jfs_map[index].jfs_flag) {
		if (from) {
			if (jfs_map[index].ext2_flag & flags)
				mapped |= jfs_map[index].jfs_flag;
		} else {
			if (jfs_map[index].jfs_flag & flags)
				mapped |= jfs_map[index].ext2_flag;
		}
		index++;
	}
	return mapped;
}

int jfs_fileattr_get(struct dentry *dentry, struct fileattr *fa)
{
	struct jfs_inode_info *jfs_inode = JFS_IP(d_inode(dentry));
	unsigned int flags = jfs_inode->mode2 & JFS_FL_USER_VISIBLE;

	if (d_is_special(dentry))
		return -ENOTTY;

	fileattr_fill_flags(fa, jfs_map_ext2(flags, 0));

	return 0;
}

int jfs_fileattr_set(struct user_namespace *mnt_userns,
		     struct dentry *dentry, struct fileattr *fa)
{
	struct inode *inode = d_inode(dentry);
	struct jfs_inode_info *jfs_inode = JFS_IP(inode);
	unsigned int flags;

	if (d_is_special(dentry))
		return -ENOTTY;

	if (fileattr_has_fsx(fa))
		return -EOPNOTSUPP;

	flags = jfs_map_ext2(fa->flags, 1);
	if (!S_ISDIR(inode->i_mode))
		flags &= ~JFS_DIRSYNC_FL;

	/* Is it quota file? Do not allow user to mess with it */
	if (IS_NOQUOTA(inode))
		return -EPERM;

	flags = flags & JFS_FL_USER_MODIFIABLE;
	flags |= jfs_inode->mode2 & ~JFS_FL_USER_MODIFIABLE;
	jfs_inode->mode2 = flags;

	jfs_set_inode_flags(inode);
	inode->i_ctime = current_time(inode);
	mark_inode_dirty(inode);

	return 0;
}

long jfs_ioctl(struct file *filp, unsigned int cmd, unsigned long arg)
{
	struct inode *inode = file_inode(filp);

	switch (cmd) {
	case FITRIM:
	{
		struct super_block *sb = inode->i_sb;
		struct fstrim_range range;
		s64 ret = 0;

		if (!capable(CAP_SYS_ADMIN))
			return -EPERM;

		if (!bdev_max_discard_sectors(sb->s_bdev)) {
			jfs_warn("FITRIM not supported on device");
			return -EOPNOTSUPP;
		}

		if (copy_from_user(&range, (struct fstrim_range __user *)arg,
		    sizeof(range)))
			return -EFAULT;

		range.minlen = max_t(unsigned int, range.minlen,
				     bdev_discard_granularity(sb->s_bdev));

		ret = jfs_ioc_trim(inode, &range);
		if (ret < 0)
			return ret;

		if (copy_to_user((struct fstrim_range __user *)arg, &range,
		    sizeof(range)))
			return -EFAULT;

		return 0;
	}

	default:
		return -ENOTTY;
	}
}
