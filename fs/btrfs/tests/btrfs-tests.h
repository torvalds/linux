/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2013 Fusion IO.  All rights reserved.
 */

#ifndef BTRFS_TESTS_H
#define BTRFS_TESTS_H

#ifdef CONFIG_BTRFS_FS_RUN_SANITY_TESTS
int btrfs_run_sanity_tests(void);

#define test_msg(fmt, ...) pr_info("BTRFS: selftest: " fmt, ##__VA_ARGS__)

struct btrfs_root;
struct btrfs_trans_handle;

int btrfs_test_extent_buffer_operations(u32 sectorsize, u32 nodesize);
int btrfs_test_free_space_cache(u32 sectorsize, u32 nodesize);
int btrfs_test_extent_io(u32 sectorsize, u32 nodesize);
int btrfs_test_inodes(u32 sectorsize, u32 nodesize);
int btrfs_test_qgroups(u32 sectorsize, u32 nodesize);
int btrfs_test_free_space_tree(u32 sectorsize, u32 nodesize);
int btrfs_test_extent_map(void);
struct inode *btrfs_new_test_inode(void);
struct btrfs_fs_info *btrfs_alloc_dummy_fs_info(u32 nodesize, u32 sectorsize);
void btrfs_free_dummy_fs_info(struct btrfs_fs_info *fs_info);
void btrfs_free_dummy_root(struct btrfs_root *root);
struct btrfs_block_group_cache *
btrfs_alloc_dummy_block_group(struct btrfs_fs_info *fs_info, unsigned long length);
void btrfs_free_dummy_block_group(struct btrfs_block_group_cache *cache);
void btrfs_init_dummy_trans(struct btrfs_trans_handle *trans);
#else
static inline int btrfs_run_sanity_tests(void)
{
	return 0;
}
#endif

#endif
