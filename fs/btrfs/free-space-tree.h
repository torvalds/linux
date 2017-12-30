/*
 * Copyright (C) 2015 Facebook.  All rights reserved.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public
 * License v2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public
 * License along with this program; if not, write to the
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 021110-1307, USA.
 */

#ifndef __BTRFS_FREE_SPACE_TREE
#define __BTRFS_FREE_SPACE_TREE

/*
 * The default size for new free space bitmap items. The last bitmap in a block
 * group may be truncated, and none of the free space tree code assumes that
 * existing bitmaps are this size.
 */
#define BTRFS_FREE_SPACE_BITMAP_SIZE 256
#define BTRFS_FREE_SPACE_BITMAP_BITS (BTRFS_FREE_SPACE_BITMAP_SIZE * BITS_PER_BYTE)

void set_free_space_tree_thresholds(struct btrfs_block_group_cache *block_group);
int btrfs_create_free_space_tree(struct btrfs_fs_info *fs_info);
int btrfs_clear_free_space_tree(struct btrfs_fs_info *fs_info);
int load_free_space_tree(struct btrfs_caching_control *caching_ctl);
int add_block_group_free_space(struct btrfs_trans_handle *trans,
			       struct btrfs_fs_info *fs_info,
			       struct btrfs_block_group_cache *block_group);
int remove_block_group_free_space(struct btrfs_trans_handle *trans,
				  struct btrfs_fs_info *fs_info,
				  struct btrfs_block_group_cache *block_group);
int add_to_free_space_tree(struct btrfs_trans_handle *trans,
			   struct btrfs_fs_info *fs_info,
			   u64 start, u64 size);
int remove_from_free_space_tree(struct btrfs_trans_handle *trans,
				struct btrfs_fs_info *fs_info,
				u64 start, u64 size);

#ifdef CONFIG_BTRFS_FS_RUN_SANITY_TESTS
struct btrfs_free_space_info *
search_free_space_info(struct btrfs_trans_handle *trans,
		       struct btrfs_fs_info *fs_info,
		       struct btrfs_block_group_cache *block_group,
		       struct btrfs_path *path, int cow);
int __add_to_free_space_tree(struct btrfs_trans_handle *trans,
			     struct btrfs_fs_info *fs_info,
			     struct btrfs_block_group_cache *block_group,
			     struct btrfs_path *path, u64 start, u64 size);
int __remove_from_free_space_tree(struct btrfs_trans_handle *trans,
				  struct btrfs_fs_info *fs_info,
				  struct btrfs_block_group_cache *block_group,
				  struct btrfs_path *path, u64 start, u64 size);
int convert_free_space_to_bitmaps(struct btrfs_trans_handle *trans,
				  struct btrfs_fs_info *fs_info,
				  struct btrfs_block_group_cache *block_group,
				  struct btrfs_path *path);
int convert_free_space_to_extents(struct btrfs_trans_handle *trans,
				  struct btrfs_fs_info *fs_info,
				  struct btrfs_block_group_cache *block_group,
				  struct btrfs_path *path);
int free_space_test_bit(struct btrfs_block_group_cache *block_group,
			struct btrfs_path *path, u64 offset);
#endif

#endif
