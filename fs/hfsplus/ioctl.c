// SPDX-License-Identifier: GPL-2.0
/*
 *  linux/fs/hfsplus/ioctl.c
 *
 * Copyright (C) 2003
 * Ethan Benson <erbenson@alaska.net>
 * partially derived from linux/fs/ext2/ioctl.c
 * Copyright (C) 1993, 1994, 1995
 * Remy Card (card@masi.ibp.fr)
 * Laboratoire MASI - Institut Blaise Pascal
 * Universite Pierre et Marie Curie (Paris VI)
 *
 * hfsplus ioctls
 */

#include <linux/capability.h>
#include <linux/fs.h>
#include <linux/mount.h>
#include <linux/sched.h>
#include <linux/uaccess.h>
#include "hfsplus_fs.h"

/*
 * "Blessing" an HFS+ filesystem writes metadata to the superblock informing
 * the platform firmware which file to boot from
 */
static int hfsplus_ioctl_bless(struct file *file, int __user *user_flags)
{
	struct dentry *dentry = file->f_path.dentry;
	struct inode *inode = d_inode(dentry);
	struct hfsplus_sb_info *sbi = HFSPLUS_SB(inode->i_sb);
	struct hfsplus_vh *vh = sbi->s_vhdr;
	struct hfsplus_vh *bvh = sbi->s_backup_vhdr;
	u32 cnid = (unsigned long)dentry->d_fsdata;

	if (!capable(CAP_SYS_ADMIN))
		return -EPERM;

	mutex_lock(&sbi->vh_mutex);

	/* Directory containing the bootable system */
	vh->finder_info[0] = bvh->finder_info[0] =
		cpu_to_be32(parent_ino(dentry));

	/*
	 * Bootloader. Just using the inode here breaks in the case of
	 * hard links - the firmware wants the ID of the hard link file,
	 * but the inode points at the indirect inode
	 */
	vh->finder_info[1] = bvh->finder_info[1] = cpu_to_be32(cnid);

	/* Per spec, the OS X system folder - same as finder_info[0] here */
	vh->finder_info[5] = bvh->finder_info[5] =
		cpu_to_be32(parent_ino(dentry));

	mutex_unlock(&sbi->vh_mutex);
	return 0;
}

static int hfsplus_ioctl_getflags(struct file *file, int __user *user_flags)
{
	struct inode *inode = file_inode(file);
	struct hfsplus_inode_info *hip = HFSPLUS_I(inode);
	unsigned int flags = 0;

	if (inode->i_flags & S_IMMUTABLE)
		flags |= FS_IMMUTABLE_FL;
	if (inode->i_flags & S_APPEND)
		flags |= FS_APPEND_FL;
	if (hip->userflags & HFSPLUS_FLG_NODUMP)
		flags |= FS_NODUMP_FL;

	return put_user(flags, user_flags);
}

static int hfsplus_ioctl_setflags(struct file *file, int __user *user_flags)
{
	struct inode *inode = file_inode(file);
	struct hfsplus_inode_info *hip = HFSPLUS_I(inode);
	unsigned int flags, new_fl = 0;
	int err = 0;

	err = mnt_want_write_file(file);
	if (err)
		goto out;

	if (!inode_owner_or_capable(inode)) {
		err = -EACCES;
		goto out_drop_write;
	}

	if (get_user(flags, user_flags)) {
		err = -EFAULT;
		goto out_drop_write;
	}

	inode_lock(inode);

	if ((flags & (FS_IMMUTABLE_FL|FS_APPEND_FL)) ||
	    inode->i_flags & (S_IMMUTABLE|S_APPEND)) {
		if (!capable(CAP_LINUX_IMMUTABLE)) {
			err = -EPERM;
			goto out_unlock_inode;
		}
	}

	/* don't silently ignore unsupported ext2 flags */
	if (flags & ~(FS_IMMUTABLE_FL|FS_APPEND_FL|FS_NODUMP_FL)) {
		err = -EOPNOTSUPP;
		goto out_unlock_inode;
	}

	if (flags & FS_IMMUTABLE_FL)
		new_fl |= S_IMMUTABLE;

	if (flags & FS_APPEND_FL)
		new_fl |= S_APPEND;

	inode_set_flags(inode, new_fl, S_IMMUTABLE | S_APPEND);

	if (flags & FS_NODUMP_FL)
		hip->userflags |= HFSPLUS_FLG_NODUMP;
	else
		hip->userflags &= ~HFSPLUS_FLG_NODUMP;

	inode->i_ctime = current_time(inode);
	mark_inode_dirty(inode);

out_unlock_inode:
	inode_unlock(inode);
out_drop_write:
	mnt_drop_write_file(file);
out:
	return err;
}

long hfsplus_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
	void __user *argp = (void __user *)arg;

	switch (cmd) {
	case HFSPLUS_IOC_EXT2_GETFLAGS:
		return hfsplus_ioctl_getflags(file, argp);
	case HFSPLUS_IOC_EXT2_SETFLAGS:
		return hfsplus_ioctl_setflags(file, argp);
	case HFSPLUS_IOC_BLESS:
		return hfsplus_ioctl_bless(file, argp);
	default:
		return -ENOTTY;
	}
}
