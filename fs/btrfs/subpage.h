/* SPDX-License-Identifier: GPL-2.0 */

#ifndef BTRFS_SUBPAGE_H
#define BTRFS_SUBPAGE_H

#include <linux/spinlock.h>

/*
 * Maximum page size we support is 64K, minimum sector size is 4K, u16 bitmap
 * is sufficient. Regular bitmap_* is not used due to size reasons.
 */
#define BTRFS_SUBPAGE_BITMAP_SIZE	16

/*
 * Structure to trace status of each sector inside a page, attached to
 * page::private for both data and metadata inodes.
 */
struct btrfs_subpage {
	/* Common members for both data and metadata pages */
	spinlock_t lock;
};

enum btrfs_subpage_type {
	BTRFS_SUBPAGE_METADATA,
	BTRFS_SUBPAGE_DATA,
};

int btrfs_attach_subpage(const struct btrfs_fs_info *fs_info,
			 struct page *page, enum btrfs_subpage_type type);
void btrfs_detach_subpage(const struct btrfs_fs_info *fs_info,
			  struct page *page);

#endif
