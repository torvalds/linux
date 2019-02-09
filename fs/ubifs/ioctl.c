/*
 * This file is part of UBIFS.
 *
 * Copyright (C) 2006-2008 Nokia Corporation.
 * Copyright (C) 2006, 2007 University of Szeged, Hungary
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published by
 * the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program; if not, write to the Free Software Foundation, Inc., 51
 * Franklin St, Fifth Floor, Boston, MA 02110-1301 USA
 *
 * Authors: Zoltan Sogor
 *          Artem Bityutskiy (Битюцкий Артём)
 *          Adrian Hunter
 */

/* This file implements EXT2-compatible extended attribute ioctl() calls */

#include <linux/compat.h>
#include <linux/mount.h>
#include "ubifs.h"

/* Need to be kept consistent with checked flags in ioctl2ubifs() */
#define UBIFS_SUPPORTED_IOCTL_FLAGS \
	(FS_COMPR_FL | FS_SYNC_FL | FS_APPEND_FL | \
	 FS_IMMUTABLE_FL | FS_DIRSYNC_FL)

/**
 * ubifs_set_inode_flags - set VFS inode flags.
 * @inode: VFS inode to set flags for
 *
 * This function propagates flags from UBIFS inode object to VFS inode object.
 */
void ubifs_set_inode_flags(struct inode *inode)
{
	unsigned int flags = ubifs_inode(inode)->flags;

	inode->i_flags &= ~(S_SYNC | S_APPEND | S_IMMUTABLE | S_DIRSYNC |
			    S_ENCRYPTED);
	if (flags & UBIFS_SYNC_FL)
		inode->i_flags |= S_SYNC;
	if (flags & UBIFS_APPEND_FL)
		inode->i_flags |= S_APPEND;
	if (flags & UBIFS_IMMUTABLE_FL)
		inode->i_flags |= S_IMMUTABLE;
	if (flags & UBIFS_DIRSYNC_FL)
		inode->i_flags |= S_DIRSYNC;
	if (flags & UBIFS_CRYPT_FL)
		inode->i_flags |= S_ENCRYPTED;
}

/*
 * ioctl2ubifs - convert ioctl inode flags to UBIFS inode flags.
 * @ioctl_flags: flags to convert
 *
 * This function converts ioctl flags (@FS_COMPR_FL, etc) to UBIFS inode flags
 * (@UBIFS_COMPR_FL, etc).
 */
static int ioctl2ubifs(int ioctl_flags)
{
	int ubifs_flags = 0;

	if (ioctl_flags & FS_COMPR_FL)
		ubifs_flags |= UBIFS_COMPR_FL;
	if (ioctl_flags & FS_SYNC_FL)
		ubifs_flags |= UBIFS_SYNC_FL;
	if (ioctl_flags & FS_APPEND_FL)
		ubifs_flags |= UBIFS_APPEND_FL;
	if (ioctl_flags & FS_IMMUTABLE_FL)
		ubifs_flags |= UBIFS_IMMUTABLE_FL;
	if (ioctl_flags & FS_DIRSYNC_FL)
		ubifs_flags |= UBIFS_DIRSYNC_FL;

	return ubifs_flags;
}

/*
 * ubifs2ioctl - convert UBIFS inode flags to ioctl inode flags.
 * @ubifs_flags: flags to convert
 *
 * This function converts UBIFS inode flags (@UBIFS_COMPR_FL, etc) to ioctl
 * flags (@FS_COMPR_FL, etc).
 */
static int ubifs2ioctl(int ubifs_flags)
{
	int ioctl_flags = 0;

	if (ubifs_flags & UBIFS_COMPR_FL)
		ioctl_flags |= FS_COMPR_FL;
	if (ubifs_flags & UBIFS_SYNC_FL)
		ioctl_flags |= FS_SYNC_FL;
	if (ubifs_flags & UBIFS_APPEND_FL)
		ioctl_flags |= FS_APPEND_FL;
	if (ubifs_flags & UBIFS_IMMUTABLE_FL)
		ioctl_flags |= FS_IMMUTABLE_FL;
	if (ubifs_flags & UBIFS_DIRSYNC_FL)
		ioctl_flags |= FS_DIRSYNC_FL;

	return ioctl_flags;
}

static int setflags(struct inode *inode, int flags)
{
	int oldflags, err, release;
	struct ubifs_inode *ui = ubifs_inode(inode);
	struct ubifs_info *c = inode->i_sb->s_fs_info;
	struct ubifs_budget_req req = { .dirtied_ino = 1,
					.dirtied_ino_d = ui->data_len };

	err = ubifs_budget_space(c, &req);
	if (err)
		return err;

	/*
	 * The IMMUTABLE and APPEND_ONLY flags can only be changed by
	 * the relevant capability.
	 */
	mutex_lock(&ui->ui_mutex);
	oldflags = ubifs2ioctl(ui->flags);
	if ((flags ^ oldflags) & (FS_APPEND_FL | FS_IMMUTABLE_FL)) {
		if (!capable(CAP_LINUX_IMMUTABLE)) {
			err = -EPERM;
			goto out_unlock;
		}
	}

	ui->flags = ioctl2ubifs(flags);
	ubifs_set_inode_flags(inode);
	inode->i_ctime = current_time(inode);
	release = ui->dirty;
	mark_inode_dirty_sync(inode);
	mutex_unlock(&ui->ui_mutex);

	if (release)
		ubifs_release_budget(c, &req);
	if (IS_SYNC(inode))
		err = write_inode_now(inode, 1);
	return err;

out_unlock:
	ubifs_err(c, "can't modify inode %lu attributes", inode->i_ino);
	mutex_unlock(&ui->ui_mutex);
	ubifs_release_budget(c, &req);
	return err;
}

long ubifs_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
	int flags, err;
	struct inode *inode = file_inode(file);

	switch (cmd) {
	case FS_IOC_GETFLAGS:
		flags = ubifs2ioctl(ubifs_inode(inode)->flags);

		dbg_gen("get flags: %#x, i_flags %#x", flags, inode->i_flags);
		return put_user(flags, (int __user *) arg);

	case FS_IOC_SETFLAGS: {
		if (IS_RDONLY(inode))
			return -EROFS;

		if (!inode_owner_or_capable(inode))
			return -EACCES;

		if (get_user(flags, (int __user *) arg))
			return -EFAULT;

		if (flags & ~UBIFS_SUPPORTED_IOCTL_FLAGS)
			return -EOPNOTSUPP;

		if (!S_ISDIR(inode->i_mode))
			flags &= ~FS_DIRSYNC_FL;

		/*
		 * Make sure the file-system is read-write and make sure it
		 * will not become read-only while we are changing the flags.
		 */
		err = mnt_want_write_file(file);
		if (err)
			return err;
		dbg_gen("set flags: %#x, i_flags %#x", flags, inode->i_flags);
		err = setflags(inode, flags);
		mnt_drop_write_file(file);
		return err;
	}
	case FS_IOC_SET_ENCRYPTION_POLICY: {
#ifdef CONFIG_UBIFS_FS_ENCRYPTION
		struct ubifs_info *c = inode->i_sb->s_fs_info;

		err = ubifs_enable_encryption(c);
		if (err)
			return err;

		return fscrypt_ioctl_set_policy(file, (const void __user *)arg);
#else
		return -EOPNOTSUPP;
#endif
	}
	case FS_IOC_GET_ENCRYPTION_POLICY: {
#ifdef CONFIG_UBIFS_FS_ENCRYPTION
		return fscrypt_ioctl_get_policy(file, (void __user *)arg);
#else
		return -EOPNOTSUPP;
#endif
	}

	default:
		return -ENOTTY;
	}
}

#ifdef CONFIG_COMPAT
long ubifs_compat_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
	switch (cmd) {
	case FS_IOC32_GETFLAGS:
		cmd = FS_IOC_GETFLAGS;
		break;
	case FS_IOC32_SETFLAGS:
		cmd = FS_IOC_SETFLAGS;
		break;
	case FS_IOC_SET_ENCRYPTION_POLICY:
	case FS_IOC_GET_ENCRYPTION_POLICY:
		break;
	default:
		return -ENOIOCTLCMD;
	}
	return ubifs_ioctl(file, cmd, (unsigned long)compat_ptr(arg));
}
#endif
