/* SPDX-License-Identifier: GPL-2.0 */

#ifndef BTRFS_VERITY_H
#define BTRFS_VERITY_H

struct inode;
struct btrfs_inode;

#ifdef CONFIG_FS_VERITY

#include <linux/fsverity.h>

extern const struct fsverity_operations btrfs_verityops;

int btrfs_drop_verity_items(struct btrfs_inode *inode);
int btrfs_get_verity_descriptor(struct inode *inode, void *buf, size_t buf_size);

#else

#include <linux/errno.h>

static inline int btrfs_drop_verity_items(struct btrfs_inode *inode)
{
	return 0;
}

static inline int btrfs_get_verity_descriptor(struct inode *inode, void *buf,
					      size_t buf_size)
{
	return -EPERM;
}

#endif

#endif
