/*
 * linux/fs/jfs/ioctl.c
 *
 * Copyright (C) 2006 Herbert Poetzl
 * adapted from Remy Card's ext2/ioctl.c
 */

#include <linux/fs.h>
#include <linux/ctype.h>
#include <linux/capability.h>
#include <linux/time.h>
#include <linux/sched.h>
#include <asm/current.h>
#include <asm/uaccess.h>

#include "jfs_incore.h"
#include "jfs_dinode.h"
#include "jfs_inode.h"


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


int jfs_ioctl(struct inode * inode, struct file * filp, unsigned int cmd,
		unsigned long arg)
{
	struct jfs_inode_info *jfs_inode = JFS_IP(inode);
	unsigned int flags;

	switch (cmd) {
	case JFS_IOC_GETFLAGS:
		jfs_get_inode_flags(jfs_inode);
		flags = jfs_inode->mode2 & JFS_FL_USER_VISIBLE;
		flags = jfs_map_ext2(flags, 0);
		return put_user(flags, (int __user *) arg);
	case JFS_IOC_SETFLAGS: {
		unsigned int oldflags;

		if (IS_RDONLY(inode))
			return -EROFS;

		if ((current->fsuid != inode->i_uid) && !capable(CAP_FOWNER))
			return -EACCES;

		if (get_user(flags, (int __user *) arg))
			return -EFAULT;

		flags = jfs_map_ext2(flags, 1);
		if (!S_ISDIR(inode->i_mode))
			flags &= ~JFS_DIRSYNC_FL;

		jfs_get_inode_flags(jfs_inode);
		oldflags = jfs_inode->mode2;

		/*
		 * The IMMUTABLE and APPEND_ONLY flags can only be changed by
		 * the relevant capability.
		 */
		if ((oldflags & JFS_IMMUTABLE_FL) ||
			((flags ^ oldflags) &
			(JFS_APPEND_FL | JFS_IMMUTABLE_FL))) {
			if (!capable(CAP_LINUX_IMMUTABLE))
				return -EPERM;
		}

		flags = flags & JFS_FL_USER_MODIFIABLE;
		flags |= oldflags & ~JFS_FL_USER_MODIFIABLE;
		jfs_inode->mode2 = flags;

		jfs_set_inode_flags(inode);
		inode->i_ctime = CURRENT_TIME_SEC;
		mark_inode_dirty(inode);
		return 0;
	}
	default:
		return -ENOTTY;
	}
}

