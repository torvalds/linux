// SPDX-License-Identifier: GPL-2.0-only
/*
 * This file is part of UBIFS.
 *
 * Copyright (C) 2006-2008 Nokia Corporation.
 * Copyright (C) 2006, 2007 University of Szeged, Hungary
 *
 * Authors: Zoltan Sogor
 *          Artem Bityutskiy (Битюцкий Артём)
 *          Adrian Hunter
 */

/* This file implements EXT2-compatible extended attribute ioctl() calls */

#include <linux/compat.h>
#include <linux/mount.h>
#include <linux/fileattr.h>
#include "ubifs.h"

/* Need to be kept consistent with checked flags in ioctl2ubifs() */
#define UBIFS_SETTABLE_IOCTL_FLAGS \
	(FS_COMPR_FL | FS_SYNC_FL | FS_APPEND_FL | \
	 FS_IMMUTABLE_FL | FS_DIRSYNC_FL)

/* Need to be kept consistent with checked flags in ubifs2ioctl() */
#define UBIFS_GETTABLE_IOCTL_FLAGS \
	(UBIFS_SETTABLE_IOCTL_FLAGS | FS_ENCRYPT_FL)

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
	if (ubifs_flags & UBIFS_CRYPT_FL)
		ioctl_flags |= FS_ENCRYPT_FL;

	return ioctl_flags;
}

static int setflags(struct inode *inode, int flags)
{
	int err, release;
	struct ubifs_inode *ui = ubifs_inode(inode);
	struct ubifs_info *c = inode->i_sb->s_fs_info;
	struct ubifs_budget_req req = { .dirtied_ino = 1,
					.dirtied_ino_d = ui->data_len };

	err = ubifs_budget_space(c, &req);
	if (err)
		return err;

	mutex_lock(&ui->ui_mutex);
	ui->flags &= ~ioctl2ubifs(UBIFS_SETTABLE_IOCTL_FLAGS);
	ui->flags |= ioctl2ubifs(flags);
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
}

int ubifs_fileattr_get(struct dentry *dentry, struct fileattr *fa)
{
	struct inode *inode = d_inode(dentry);
	int flags = ubifs2ioctl(ubifs_inode(inode)->flags);

	if (d_is_special(dentry))
		return -ENOTTY;

	dbg_gen("get flags: %#x, i_flags %#x", flags, inode->i_flags);
	fileattr_fill_flags(fa, flags);

	return 0;
}

int ubifs_fileattr_set(struct user_namespace *mnt_userns,
		       struct dentry *dentry, struct fileattr *fa)
{
	struct inode *inode = d_inode(dentry);
	int flags = fa->flags;

	if (d_is_special(dentry))
		return -ENOTTY;

	if (fileattr_has_fsx(fa))
		return -EOPNOTSUPP;

	if (flags & ~UBIFS_GETTABLE_IOCTL_FLAGS)
		return -EOPNOTSUPP;

	flags &= UBIFS_SETTABLE_IOCTL_FLAGS;

	if (!S_ISDIR(inode->i_mode))
		flags &= ~FS_DIRSYNC_FL;

	dbg_gen("set flags: %#x, i_flags %#x", flags, inode->i_flags);
	return setflags(inode, flags);
}

long ubifs_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
	int err;
	struct inode *inode = file_inode(file);

	switch (cmd) {
	case FS_IOC_SET_ENCRYPTION_POLICY: {
		struct ubifs_info *c = inode->i_sb->s_fs_info;

		err = ubifs_enable_encryption(c);
		if (err)
			return err;

		return fscrypt_ioctl_set_policy(file, (const void __user *)arg);
	}
	case FS_IOC_GET_ENCRYPTION_POLICY:
		return fscrypt_ioctl_get_policy(file, (void __user *)arg);

	case FS_IOC_GET_ENCRYPTION_POLICY_EX:
		return fscrypt_ioctl_get_policy_ex(file, (void __user *)arg);

	case FS_IOC_ADD_ENCRYPTION_KEY:
		return fscrypt_ioctl_add_key(file, (void __user *)arg);

	case FS_IOC_REMOVE_ENCRYPTION_KEY:
		return fscrypt_ioctl_remove_key(file, (void __user *)arg);

	case FS_IOC_REMOVE_ENCRYPTION_KEY_ALL_USERS:
		return fscrypt_ioctl_remove_key_all_users(file,
							  (void __user *)arg);
	case FS_IOC_GET_ENCRYPTION_KEY_STATUS:
		return fscrypt_ioctl_get_key_status(file, (void __user *)arg);

	case FS_IOC_GET_ENCRYPTION_NONCE:
		return fscrypt_ioctl_get_nonce(file, (void __user *)arg);

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
	case FS_IOC_GET_ENCRYPTION_POLICY_EX:
	case FS_IOC_ADD_ENCRYPTION_KEY:
	case FS_IOC_REMOVE_ENCRYPTION_KEY:
	case FS_IOC_REMOVE_ENCRYPTION_KEY_ALL_USERS:
	case FS_IOC_GET_ENCRYPTION_KEY_STATUS:
	case FS_IOC_GET_ENCRYPTION_NONCE:
		break;
	default:
		return -ENOIOCTLCMD;
	}
	return ubifs_ioctl(file, cmd, (unsigned long)compat_ptr(arg));
}
#endif
