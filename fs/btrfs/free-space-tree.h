/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2015 Facebook.  All rights reserved.
 */

#ifndef BTRFS_FREE_SPACE_TREE_H
#define BTRFS_FREE_SPACE_TREE_H

#include <linux/bits.h>

struct btrfs_caching_control;
struct btrfs_fs_info;
struct btrfs_path;
struct btrfs_block_group;
struct btrfs_trans_handle;

/*
 * The default size for new free space bitmap items. The last bitmap in a block
 * group may be truncated, and none of the free space tree code assumes that
 * existing bitmaps are this size.
 */
#define BTRFS_FREE_SPACE_BITMAP_SIZE 256
#define BTRFS_FREE_SPACE_BITMAP_BITS (BTRFS_FREE_SPACE_BITMAP_SIZE * BITS_PER_BYTE)

void btrfs_set_free_space_tree_thresholds(struct btrfs_block_group *block_group);
int btrfs_create_free_space_tree(struct btrfs_fs_info *fs_info);
int btrfs_delete_free_space_tree(struct btrfs_fs_info *fs_info);
int btrfs_rebuild_free_space_tree(struct btrfs_fs_info *fs_info);
int btrfs_load_free_space_tree(struct btrfs_caching_control *caching_ctl);
int btrfs_add_block_group_free_space(struct btrfs_trans_handle *trans,
				     struct btrfs_block_group *block_group);
int btrfs_remove_block_group_free_space(struct btrfs_trans_handle *trans,
					struct btrfs_block_group *block_group);
int btrfs_add_to_free_space_tree(struct btrfs_trans_handle *trans,
				 u64 start, u64 size);
int btrfs_remove_from_free_space_tree(struct btrfs_trans_handle *trans,
				      u64 start, u64 size);

#ifdef CONFIG_BTRFS_FS_RUN_SANITY_TESTS
struct btrfs_free_space_info *
btrfs_search_free_space_info(struct btrfs_trans_handle *trans,
			     struct btrfs_block_group *block_group,
			     struct btrfs_path *path, int cow);
int __btrfs_add_to_free_space_tree(struct btrfs_trans_handle *trans,
				   struct btrfs_block_group *block_group,
				   struct btrfs_path *path, u64 start, u64 size);
int __btrfs_remove_from_free_space_tree(struct btrfs_trans_handle *trans,
					struct btrfs_block_group *block_group,
					struct btrfs_path *path, u64 start, u64 size);
int btrfs_convert_free_space_to_bitmaps(struct btrfs_trans_handle *trans,
					struct btrfs_block_group *block_group,
					struct btrfs_path *path);
int btrfs_convert_free_space_to_extents(struct btrfs_trans_handle *trans,
					struct btrfs_block_group *block_group,
					struct btrfs_path *path);
bool btrfs_free_space_test_bit(struct btrfs_block_group *block_group,
			       struct btrfs_path *path, u64 offset);
#endif

#endif
