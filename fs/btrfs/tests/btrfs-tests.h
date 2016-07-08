/*
 * Copyright (C) 2013 Fusion IO.  All rights reserved.
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

#ifndef __BTRFS_TESTS
#define __BTRFS_TESTS

#ifdef CONFIG_BTRFS_FS_RUN_SANITY_TESTS

#define test_msg(fmt, ...) pr_info("BTRFS: selftest: " fmt, ##__VA_ARGS__)

struct btrfs_root;
struct btrfs_trans_handle;

int btrfs_test_free_space_cache(u32 sectorsize, u32 nodesize);
int btrfs_test_extent_buffer_operations(u32 sectorsize, u32 nodesize);
int btrfs_test_extent_io(u32 sectorsize, u32 nodesize);
int btrfs_test_inodes(u32 sectorsize, u32 nodesize);
int btrfs_test_qgroups(u32 sectorsize, u32 nodesize);
int btrfs_test_free_space_tree(u32 sectorsize, u32 nodesize);
int btrfs_init_test_fs(void);
void btrfs_destroy_test_fs(void);
struct inode *btrfs_new_test_inode(void);
struct btrfs_fs_info *btrfs_alloc_dummy_fs_info(void);
void btrfs_free_dummy_root(struct btrfs_root *root);
struct btrfs_block_group_cache *
btrfs_alloc_dummy_block_group(unsigned long length, u32 sectorsize);
void btrfs_free_dummy_block_group(struct btrfs_block_group_cache *cache);
void btrfs_init_dummy_trans(struct btrfs_trans_handle *trans);
#else
static inline int btrfs_test_free_space_cache(u32 sectorsize, u32 nodesize)
{
	return 0;
}
static inline int btrfs_test_extent_buffer_operations(u32 sectorsize,
	u32 nodesize)
{
	return 0;
}
static inline int btrfs_init_test_fs(void)
{
	return 0;
}
static inline void btrfs_destroy_test_fs(void)
{
}
static inline int btrfs_test_extent_io(u32 sectorsize, u32 nodesize)
{
	return 0;
}
static inline int btrfs_test_inodes(u32 sectorsize, u32 nodesize)
{
	return 0;
}
static inline int btrfs_test_qgroups(u32 sectorsize, u32 nodesize)
{
	return 0;
}
static inline int btrfs_test_free_space_tree(u32 sectorsize, u32 nodesize)
{
	return 0;
}
#endif

#endif
