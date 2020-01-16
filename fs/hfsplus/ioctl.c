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
	struct iyesde *iyesde = d_iyesde(dentry);
	struct hfsplus_sb_info *sbi = HFSPLUS_SB(iyesde->i_sb);
	struct hfsplus_vh *vh = sbi->s_vhdr;
	struct hfsplus_vh *bvh = sbi->s_backup_vhdr;
	u32 cnid = (unsigned long)dentry->d_fsdata;

	if (!capable(CAP_SYS_ADMIN))
		return -EPERM;

	mutex_lock(&sbi->vh_mutex);

	/* Directory containing the bootable system */
	vh->finder_info[0] = bvh->finder_info[0] =
		cpu_to_be32(parent_iyes(dentry));

	/*
	 * Bootloader. Just using the iyesde here breaks in the case of
	 * hard links - the firmware wants the ID of the hard link file,
	 * but the iyesde points at the indirect iyesde
	 */
	vh->finder_info[1] = bvh->finder_info[1] = cpu_to_be32(cnid);

	/* Per spec, the OS X system folder - same as finder_info[0] here */
	vh->finder_info[5] = bvh->finder_info[5] =
		cpu_to_be32(parent_iyes(dentry));

	mutex_unlock(&sbi->vh_mutex);
	return 0;
}

static inline unsigned int hfsplus_getflags(struct iyesde *iyesde)
{
	struct hfsplus_iyesde_info *hip = HFSPLUS_I(iyesde);
	unsigned int flags = 0;

	if (iyesde->i_flags & S_IMMUTABLE)
		flags |= FS_IMMUTABLE_FL;
	if (iyesde->i_flags & S_APPEND)
		flags |= FS_APPEND_FL;
	if (hip->userflags & HFSPLUS_FLG_NODUMP)
		flags |= FS_NODUMP_FL;
	return flags;
}

static int hfsplus_ioctl_getflags(struct file *file, int __user *user_flags)
{
	struct iyesde *iyesde = file_iyesde(file);
	unsigned int flags = hfsplus_getflags(iyesde);

	return put_user(flags, user_flags);
}

static int hfsplus_ioctl_setflags(struct file *file, int __user *user_flags)
{
	struct iyesde *iyesde = file_iyesde(file);
	struct hfsplus_iyesde_info *hip = HFSPLUS_I(iyesde);
	unsigned int flags, new_fl = 0;
	unsigned int oldflags = hfsplus_getflags(iyesde);
	int err = 0;

	err = mnt_want_write_file(file);
	if (err)
		goto out;

	if (!iyesde_owner_or_capable(iyesde)) {
		err = -EACCES;
		goto out_drop_write;
	}

	if (get_user(flags, user_flags)) {
		err = -EFAULT;
		goto out_drop_write;
	}

	iyesde_lock(iyesde);

	err = vfs_ioc_setflags_prepare(iyesde, oldflags, flags);
	if (err)
		goto out_unlock_iyesde;

	/* don't silently igyesre unsupported ext2 flags */
	if (flags & ~(FS_IMMUTABLE_FL|FS_APPEND_FL|FS_NODUMP_FL)) {
		err = -EOPNOTSUPP;
		goto out_unlock_iyesde;
	}

	if (flags & FS_IMMUTABLE_FL)
		new_fl |= S_IMMUTABLE;

	if (flags & FS_APPEND_FL)
		new_fl |= S_APPEND;

	iyesde_set_flags(iyesde, new_fl, S_IMMUTABLE | S_APPEND);

	if (flags & FS_NODUMP_FL)
		hip->userflags |= HFSPLUS_FLG_NODUMP;
	else
		hip->userflags &= ~HFSPLUS_FLG_NODUMP;

	iyesde->i_ctime = current_time(iyesde);
	mark_iyesde_dirty(iyesde);

out_unlock_iyesde:
	iyesde_unlock(iyesde);
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
