/* SPDX-License-Identifier: GPL-2.0 */

#ifndef BTRFS_IOCTL_H
#define BTRFS_IOCTL_H

#include <linux/types.h>

struct file;
struct dentry;
struct mnt_idmap;
struct file_kattr;
struct io_uring_cmd;
struct btrfs_inode;
struct btrfs_fs_info;
struct btrfs_ioctl_balance_args;

long btrfs_ioctl(struct file *file, unsigned int cmd, unsigned long arg);
long btrfs_compat_ioctl(struct file *file, unsigned int cmd, unsigned long arg);
int btrfs_fileattr_get(struct dentry *dentry, struct file_kattr *fa);
int btrfs_fileattr_set(struct mnt_idmap *idmap,
		       struct dentry *dentry, struct file_kattr *fa);
int btrfs_ioctl_get_supported_features(void __user *arg);
void btrfs_sync_inode_flags_to_i_flags(struct btrfs_inode *inode);
void btrfs_update_ioctl_balance_args(struct btrfs_fs_info *fs_info,
				     struct btrfs_ioctl_balance_args *bargs);
int btrfs_uring_cmd(struct io_uring_cmd *cmd, unsigned int issue_flags);
void btrfs_uring_read_extent_endio(void *ctx, int err);

#endif
