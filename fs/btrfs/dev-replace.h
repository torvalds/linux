/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) STRATO AG 2012.  All rights reserved.
 */

#ifndef BTRFS_DEV_REPLACE_H
#define BTRFS_DEV_REPLACE_H

struct btrfs_ioctl_dev_replace_args;

int btrfs_init_dev_replace(struct btrfs_fs_info *fs_info);
int btrfs_run_dev_replace(struct btrfs_trans_handle *trans,
			  struct btrfs_fs_info *fs_info);
int btrfs_dev_replace_by_ioctl(struct btrfs_fs_info *fs_info,
			    struct btrfs_ioctl_dev_replace_args *args);
int btrfs_dev_replace_start(struct btrfs_fs_info *fs_info,
		const char *tgtdev_name, u64 srcdevid, const char *srcdev_name,
		int read_src);
void btrfs_dev_replace_status(struct btrfs_fs_info *fs_info,
			      struct btrfs_ioctl_dev_replace_args *args);
int btrfs_dev_replace_cancel(struct btrfs_fs_info *fs_info);
void btrfs_dev_replace_suspend_for_unmount(struct btrfs_fs_info *fs_info);
int btrfs_resume_dev_replace_async(struct btrfs_fs_info *fs_info);
int btrfs_dev_replace_is_ongoing(struct btrfs_dev_replace *dev_replace);
void btrfs_dev_replace_read_lock(struct btrfs_dev_replace *dev_replace);
void btrfs_dev_replace_read_unlock(struct btrfs_dev_replace *dev_replace);
void btrfs_dev_replace_write_lock(struct btrfs_dev_replace *dev_replace);
void btrfs_dev_replace_write_unlock(struct btrfs_dev_replace *dev_replace);
void btrfs_dev_replace_set_lock_blocking(struct btrfs_dev_replace *dev_replace);

#endif
