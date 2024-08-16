/* SPDX-License-Identifier: GPL-2.0 */

#ifndef BTRFS_IOCTL_H
#define BTRFS_IOCTL_H

#include <linux/types.h>

struct file;
struct dentry;
struct mnt_idmap;
struct fileattr;
struct btrfs_fs_info;
struct btrfs_ioctl_balance_args;

long btrfs_ioctl(struct file *file, unsigned int cmd, unsigned long arg);
long btrfs_compat_ioctl(struct file *file, unsigned int cmd, unsigned long arg);
int btrfs_fileattr_get(struct dentry *dentry, struct fileattr *fa);
int btrfs_fileattr_set(struct mnt_idmap *idmap,
		       struct dentry *dentry, struct fileattr *fa);
int btrfs_ioctl_get_supported_features(void __user *arg);
void btrfs_sync_inode_flags_to_i_flags(struct inode *inode);
int __pure btrfs_is_empty_uuid(const u8 *uuid);
void btrfs_update_ioctl_balance_args(struct btrfs_fs_info *fs_info,
				     struct btrfs_ioctl_balance_args *bargs);

#endif
