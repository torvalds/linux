/* SPDX-License-Identifier: GPL-2.0 */

#ifndef BTRFS_FIEMAP_H
#define BTRFS_FIEMAP_H

#include <linux/fiemap.h>

int btrfs_fiemap(struct inode *inode, struct fiemap_extent_info *fieinfo,
		 u64 start, u64 len);

#endif /* BTRFS_FIEMAP_H */
