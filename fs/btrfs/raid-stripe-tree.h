/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2023 Western Digital Corporation or its affiliates.
 */

#ifndef BTRFS_RAID_STRIPE_TREE_H
#define BTRFS_RAID_STRIPE_TREE_H

#include <linux/types.h>
#include <uapi/linux/btrfs_tree.h>
#include "fs.h"

#define BTRFS_RST_SUPP_BLOCK_GROUP_MASK    (BTRFS_BLOCK_GROUP_DUP |		\
					    BTRFS_BLOCK_GROUP_RAID1_MASK |	\
					    BTRFS_BLOCK_GROUP_RAID0 |		\
					    BTRFS_BLOCK_GROUP_RAID10)

struct btrfs_io_context;
struct btrfs_io_stripe;
struct btrfs_fs_info;
struct btrfs_ordered_extent;
struct btrfs_trans_handle;

int btrfs_delete_raid_extent(struct btrfs_trans_handle *trans, u64 start, u64 length);
int btrfs_get_raid_extent_offset(struct btrfs_fs_info *fs_info,
				 u64 logical, u64 *length, u64 map_type,
				 u32 stripe_index, struct btrfs_io_stripe *stripe);
int btrfs_insert_raid_extent(struct btrfs_trans_handle *trans,
			     struct btrfs_ordered_extent *ordered_extent);

static inline bool btrfs_need_stripe_tree_update(struct btrfs_fs_info *fs_info,
						 u64 map_type)
{
	u64 type = map_type & BTRFS_BLOCK_GROUP_TYPE_MASK;
	u64 profile = map_type & BTRFS_BLOCK_GROUP_PROFILE_MASK;

	if (!btrfs_fs_incompat(fs_info, RAID_STRIPE_TREE))
		return false;

	if (type != BTRFS_BLOCK_GROUP_DATA)
		return false;

	if (profile & BTRFS_RST_SUPP_BLOCK_GROUP_MASK)
		return true;

	return false;
}

static inline int btrfs_num_raid_stripes(u32 item_size)
{
	return (item_size - offsetof(struct btrfs_stripe_extent, strides)) /
		sizeof(struct btrfs_raid_stride);
}

#endif
