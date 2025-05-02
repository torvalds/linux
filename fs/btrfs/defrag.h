/* SPDX-License-Identifier: GPL-2.0 */

#ifndef BTRFS_DEFRAG_H
#define BTRFS_DEFRAG_H

#include <linux/types.h>
#include <linux/compiler_types.h>

struct file_ra_state;
struct btrfs_inode;
struct btrfs_fs_info;
struct btrfs_root;
struct btrfs_trans_handle;
struct btrfs_ioctl_defrag_range_args;

int btrfs_defrag_file(struct btrfs_inode *inode, struct file_ra_state *ra,
		      struct btrfs_ioctl_defrag_range_args *range,
		      u64 newer_than, unsigned long max_to_defrag);
int __init btrfs_auto_defrag_init(void);
void __cold btrfs_auto_defrag_exit(void);
void btrfs_add_inode_defrag(struct btrfs_inode *inode, u32 extent_thresh);
int btrfs_run_defrag_inodes(struct btrfs_fs_info *fs_info);
void btrfs_cleanup_defrag_inodes(struct btrfs_fs_info *fs_info);
int btrfs_defrag_root(struct btrfs_root *root);

static inline int btrfs_defrag_cancelled(struct btrfs_fs_info *fs_info)
{
	return signal_pending(current);
}

#endif
