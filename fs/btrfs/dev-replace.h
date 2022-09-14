/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) STRATO AG 2012.  All rights reserved.
 */

#ifndef BTRFS_DEV_REPLACE_H
#define BTRFS_DEV_REPLACE_H

struct btrfs_ioctl_dev_replace_args;
struct btrfs_fs_info;
struct btrfs_trans_handle;
struct btrfs_dev_replace;
struct btrfs_block_group;

int btrfs_init_dev_replace(struct btrfs_fs_info *fs_info);
int btrfs_run_dev_replace(struct btrfs_trans_handle *trans);
int btrfs_dev_replace_by_ioctl(struct btrfs_fs_info *fs_info,
			    struct btrfs_ioctl_dev_replace_args *args);
void btrfs_dev_replace_status(struct btrfs_fs_info *fs_info,
			      struct btrfs_ioctl_dev_replace_args *args);
int btrfs_dev_replace_cancel(struct btrfs_fs_info *fs_info);
void btrfs_dev_replace_suspend_for_unmount(struct btrfs_fs_info *fs_info);
int btrfs_resume_dev_replace_async(struct btrfs_fs_info *fs_info);
int __pure btrfs_dev_replace_is_ongoing(struct btrfs_dev_replace *dev_replace);
bool btrfs_finish_block_group_to_copy(struct btrfs_device *srcdev,
				      struct btrfs_block_group *cache,
				      u64 physical);

#endif
