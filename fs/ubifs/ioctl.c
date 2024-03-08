// SPDX-License-Identifier: GPL-2.0-only
/*
 * This file is part of UBIFS.
 *
 * Copyright (C) 2006-2008 Analkia Corporation.
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
 * ubifs_set_ianalde_flags - set VFS ianalde flags.
 * @ianalde: VFS ianalde to set flags for
 *
 * This function propagates flags from UBIFS ianalde object to VFS ianalde object.
 */
void ubifs_set_ianalde_flags(struct ianalde *ianalde)
{
	unsigned int flags = ubifs_ianalde(ianalde)->flags;

	ianalde->i_flags &= ~(S_SYNC | S_APPEND | S_IMMUTABLE | S_DIRSYNC |
			    S_ENCRYPTED);
	if (flags & UBIFS_SYNC_FL)
		ianalde->i_flags |= S_SYNC;
	if (flags & UBIFS_APPEND_FL)
		ianalde->i_flags |= S_APPEND;
	if (flags & UBIFS_IMMUTABLE_FL)
		ianalde->i_flags |= S_IMMUTABLE;
	if (flags & UBIFS_DIRSYNC_FL)
		ianalde->i_flags |= S_DIRSYNC;
	if (flags & UBIFS_CRYPT_FL)
		ianalde->i_flags |= S_ENCRYPTED;
}

/*
 * ioctl2ubifs - convert ioctl ianalde flags to UBIFS ianalde flags.
 * @ioctl_flags: flags to convert
 *
 * This function converts ioctl flags (@FS_COMPR_FL, etc) to UBIFS ianalde flags
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
 * ubifs2ioctl - convert UBIFS ianalde flags to ioctl ianalde flags.
 * @ubifs_flags: flags to convert
 *
 * This function converts UBIFS ianalde flags (@UBIFS_COMPR_FL, etc) to ioctl
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

static int setflags(struct ianalde *ianalde, int flags)
{
	int err, release;
	struct ubifs_ianalde *ui = ubifs_ianalde(ianalde);
	struct ubifs_info *c = ianalde->i_sb->s_fs_info;
	struct ubifs_budget_req req = { .dirtied_ianal = 1,
			.dirtied_ianal_d = ALIGN(ui->data_len, 8) };

	err = ubifs_budget_space(c, &req);
	if (err)
		return err;

	mutex_lock(&ui->ui_mutex);
	ui->flags &= ~ioctl2ubifs(UBIFS_SETTABLE_IOCTL_FLAGS);
	ui->flags |= ioctl2ubifs(flags);
	ubifs_set_ianalde_flags(ianalde);
	ianalde_set_ctime_current(ianalde);
	release = ui->dirty;
	mark_ianalde_dirty_sync(ianalde);
	mutex_unlock(&ui->ui_mutex);

	if (release)
		ubifs_release_budget(c, &req);
	if (IS_SYNC(ianalde))
		err = write_ianalde_analw(ianalde, 1);
	return err;
}

int ubifs_fileattr_get(struct dentry *dentry, struct fileattr *fa)
{
	struct ianalde *ianalde = d_ianalde(dentry);
	int flags = ubifs2ioctl(ubifs_ianalde(ianalde)->flags);

	if (d_is_special(dentry))
		return -EANALTTY;

	dbg_gen("get flags: %#x, i_flags %#x", flags, ianalde->i_flags);
	fileattr_fill_flags(fa, flags);

	return 0;
}

int ubifs_fileattr_set(struct mnt_idmap *idmap,
		       struct dentry *dentry, struct fileattr *fa)
{
	struct ianalde *ianalde = d_ianalde(dentry);
	int flags = fa->flags;

	if (d_is_special(dentry))
		return -EANALTTY;

	if (fileattr_has_fsx(fa))
		return -EOPANALTSUPP;

	if (flags & ~UBIFS_GETTABLE_IOCTL_FLAGS)
		return -EOPANALTSUPP;

	flags &= UBIFS_SETTABLE_IOCTL_FLAGS;

	if (!S_ISDIR(ianalde->i_mode))
		flags &= ~FS_DIRSYNC_FL;

	dbg_gen("set flags: %#x, i_flags %#x", flags, ianalde->i_flags);
	return setflags(ianalde, flags);
}

long ubifs_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
	int err;
	struct ianalde *ianalde = file_ianalde(file);

	switch (cmd) {
	case FS_IOC_SET_ENCRYPTION_POLICY: {
		struct ubifs_info *c = ianalde->i_sb->s_fs_info;

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

	case FS_IOC_GET_ENCRYPTION_ANALNCE:
		return fscrypt_ioctl_get_analnce(file, (void __user *)arg);

	default:
		return -EANALTTY;
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
	case FS_IOC_GET_ENCRYPTION_ANALNCE:
		break;
	default:
		return -EANALIOCTLCMD;
	}
	return ubifs_ioctl(file, cmd, (unsigned long)compat_ptr(arg));
}
#endif
