/*
 * Copyright (C) 2011 STRATO.  All rights reserved.
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

#ifndef __BTRFS_BACKREF__
#define __BTRFS_BACKREF__

#include "ioctl.h"
#include "ulist.h"

#define BTRFS_BACKREF_SEARCH_COMMIT_ROOT ((struct btrfs_trans_handle *)0)

struct inode_fs_paths {
	struct btrfs_path		*btrfs_path;
	struct btrfs_root		*fs_root;
	struct btrfs_data_container	*fspath;
};

typedef int (iterate_extent_inodes_t)(u64 inum, u64 offset, u64 root,
		void *ctx);
typedef int (iterate_irefs_t)(u64 parent, struct btrfs_inode_ref *iref,
				struct extent_buffer *eb, void *ctx);

int inode_item_info(u64 inum, u64 ioff, struct btrfs_root *fs_root,
			struct btrfs_path *path);

int extent_from_logical(struct btrfs_fs_info *fs_info, u64 logical,
			struct btrfs_path *path, struct btrfs_key *found_key);

int tree_backref_for_extent(unsigned long *ptr, struct extent_buffer *eb,
				struct btrfs_extent_item *ei, u32 item_size,
				u64 *out_root, u8 *out_level);

int iterate_extent_inodes(struct btrfs_fs_info *fs_info,
				u64 extent_item_objectid,
				u64 extent_offset, int search_commit_root,
				iterate_extent_inodes_t *iterate, void *ctx);

int iterate_inodes_from_logical(u64 logical, struct btrfs_fs_info *fs_info,
				struct btrfs_path *path,
				iterate_extent_inodes_t *iterate, void *ctx);

int paths_from_inode(u64 inum, struct inode_fs_paths *ipath);

int btrfs_find_all_roots(struct btrfs_trans_handle *trans,
				struct btrfs_fs_info *fs_info, u64 bytenr,
				u64 seq, struct ulist **roots);

struct btrfs_data_container *init_data_container(u32 total_bytes);
struct inode_fs_paths *init_ipath(s32 total_bytes, struct btrfs_root *fs_root,
					struct btrfs_path *path);
void free_ipath(struct inode_fs_paths *ipath);

#endif
