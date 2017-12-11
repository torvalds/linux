/*
 * Copyright (C) 2014 Facebook.  All rights reserved.
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
#ifndef __REF_VERIFY__
#define __REF_VERIFY__

#ifdef CONFIG_BTRFS_FS_REF_VERIFY
int btrfs_build_ref_tree(struct btrfs_fs_info *fs_info);
void btrfs_free_ref_cache(struct btrfs_fs_info *fs_info);
int btrfs_ref_tree_mod(struct btrfs_root *root, u64 bytenr, u64 num_bytes,
		       u64 parent, u64 ref_root, u64 owner, u64 offset,
		       int action);
void btrfs_free_ref_tree_range(struct btrfs_fs_info *fs_info, u64 start,
			       u64 len);

static inline void btrfs_init_ref_verify(struct btrfs_fs_info *fs_info)
{
	spin_lock_init(&fs_info->ref_verify_lock);
	fs_info->block_tree = RB_ROOT;
}
#else
static inline int btrfs_build_ref_tree(struct btrfs_fs_info *fs_info)
{
	return 0;
}

static inline void btrfs_free_ref_cache(struct btrfs_fs_info *fs_info)
{
}

static inline int btrfs_ref_tree_mod(struct btrfs_root *root, u64 bytenr,
				     u64 num_bytes, u64 parent, u64 ref_root,
				     u64 owner, u64 offset, int action)
{
	return 0;
}

static inline void btrfs_free_ref_tree_range(struct btrfs_fs_info *fs_info,
					     u64 start, u64 len)
{
}

static inline void btrfs_init_ref_verify(struct btrfs_fs_info *fs_info)
{
}

#endif /* CONFIG_BTRFS_FS_REF_VERIFY */
#endif /* _REF_VERIFY__ */
