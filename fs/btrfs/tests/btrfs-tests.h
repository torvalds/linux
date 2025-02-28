/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2013 Fusion IO.  All rights reserved.
 */

#ifndef BTRFS_TESTS_H
#define BTRFS_TESTS_H

#include <linux/types.h>

#ifdef CONFIG_BTRFS_FS_RUN_SANITY_TESTS
int btrfs_run_sanity_tests(void);

#define test_msg(fmt, ...) pr_info("BTRFS: selftest: " fmt "\n", ##__VA_ARGS__)
#define test_err(fmt, ...) pr_err("BTRFS: selftest: %s:%d " fmt "\n",	\
		__FILE__, __LINE__, ##__VA_ARGS__)

#define test_std_err(index)	test_err("%s", test_error[index])

enum {
	TEST_ALLOC_FS_INFO,
	TEST_ALLOC_ROOT,
	TEST_ALLOC_EXTENT_BUFFER,
	TEST_ALLOC_PATH,
	TEST_ALLOC_INODE,
	TEST_ALLOC_BLOCK_GROUP,
	TEST_ALLOC_EXTENT_MAP,
	TEST_ALLOC_CHUNK_MAP,
	TEST_ALLOC_IO_CONTEXT,
	TEST_ALLOC_TRANSACTION,
};

extern const char *test_error[];

struct btrfs_root;
struct btrfs_trans_handle;
struct btrfs_transaction;

int btrfs_test_extent_buffer_operations(u32 sectorsize, u32 nodesize);
int btrfs_test_free_space_cache(u32 sectorsize, u32 nodesize);
int btrfs_test_extent_io(u32 sectorsize, u32 nodesize);
int btrfs_test_inodes(u32 sectorsize, u32 nodesize);
int btrfs_test_qgroups(u32 sectorsize, u32 nodesize);
int btrfs_test_free_space_tree(u32 sectorsize, u32 nodesize);
int btrfs_test_raid_stripe_tree(u32 sectorsize, u32 nodesize);
int btrfs_test_extent_map(void);
int btrfs_test_delayed_refs(u32 sectorsize, u32 nodesize);
struct inode *btrfs_new_test_inode(void);
struct btrfs_fs_info *btrfs_alloc_dummy_fs_info(u32 nodesize, u32 sectorsize);
void btrfs_free_dummy_fs_info(struct btrfs_fs_info *fs_info);
void btrfs_free_dummy_root(struct btrfs_root *root);
struct btrfs_block_group *
btrfs_alloc_dummy_block_group(struct btrfs_fs_info *fs_info, unsigned long length);
void btrfs_free_dummy_block_group(struct btrfs_block_group *cache);
void btrfs_init_dummy_trans(struct btrfs_trans_handle *trans,
			    struct btrfs_fs_info *fs_info);
void btrfs_init_dummy_transaction(struct btrfs_transaction *trans, struct btrfs_fs_info *fs_info);
struct btrfs_device *btrfs_alloc_dummy_device(struct btrfs_fs_info *fs_info);
#else
static inline int btrfs_run_sanity_tests(void)
{
	return 0;
}
#endif

#endif
