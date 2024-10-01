// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2017 Oracle.  All rights reserved.
 */

#include <linux/types.h>
#include "btrfs-tests.h"
#include "../ctree.h"
#include "../volumes.h"
#include "../disk-io.h"
#include "../block-group.h"

static void free_extent_map_tree(struct extent_map_tree *em_tree)
{
	struct extent_map *em;
	struct rb_node *node;

	write_lock(&em_tree->lock);
	while (!RB_EMPTY_ROOT(&em_tree->map.rb_root)) {
		node = rb_first_cached(&em_tree->map);
		em = rb_entry(node, struct extent_map, rb_node);
		remove_extent_mapping(em_tree, em);

#ifdef CONFIG_BTRFS_DEBUG
		if (refcount_read(&em->refs) != 1) {
			test_err(
"em leak: em (start 0x%llx len 0x%llx block_start 0x%llx block_len 0x%llx) refs %d",
				 em->start, em->len, em->block_start,
				 em->block_len, refcount_read(&em->refs));

			refcount_set(&em->refs, 1);
		}
#endif
		free_extent_map(em);
	}
	write_unlock(&em_tree->lock);
}

/*
 * Test scenario:
 *
 * Suppose that no extent map has been loaded into memory yet, there is a file
 * extent [0, 16K), followed by another file extent [16K, 20K), two dio reads
 * are entering btrfs_get_extent() concurrently, t1 is reading [8K, 16K), t2 is
 * reading [0, 8K)
 *
 *     t1                            t2
 *  btrfs_get_extent()              btrfs_get_extent()
 *    -> lookup_extent_mapping()      ->lookup_extent_mapping()
 *    -> add_extent_mapping(0, 16K)
 *    -> return em
 *                                    ->add_extent_mapping(0, 16K)
 *                                    -> #handle -EEXIST
 */
static int test_case_1(struct btrfs_fs_info *fs_info,
		struct extent_map_tree *em_tree)
{
	struct extent_map *em;
	u64 start = 0;
	u64 len = SZ_8K;
	int ret;

	em = alloc_extent_map();
	if (!em) {
		test_std_err(TEST_ALLOC_EXTENT_MAP);
		return -ENOMEM;
	}

	/* Add [0, 16K) */
	em->start = 0;
	em->len = SZ_16K;
	em->block_start = 0;
	em->block_len = SZ_16K;
	write_lock(&em_tree->lock);
	ret = add_extent_mapping(em_tree, em, 0);
	write_unlock(&em_tree->lock);
	if (ret < 0) {
		test_err("cannot add extent range [0, 16K)");
		goto out;
	}
	free_extent_map(em);

	/* Add [16K, 20K) following [0, 16K)  */
	em = alloc_extent_map();
	if (!em) {
		test_std_err(TEST_ALLOC_EXTENT_MAP);
		ret = -ENOMEM;
		goto out;
	}

	em->start = SZ_16K;
	em->len = SZ_4K;
	em->block_start = SZ_32K; /* avoid merging */
	em->block_len = SZ_4K;
	write_lock(&em_tree->lock);
	ret = add_extent_mapping(em_tree, em, 0);
	write_unlock(&em_tree->lock);
	if (ret < 0) {
		test_err("cannot add extent range [16K, 20K)");
		goto out;
	}
	free_extent_map(em);

	em = alloc_extent_map();
	if (!em) {
		test_std_err(TEST_ALLOC_EXTENT_MAP);
		ret = -ENOMEM;
		goto out;
	}

	/* Add [0, 8K), should return [0, 16K) instead. */
	em->start = start;
	em->len = len;
	em->block_start = start;
	em->block_len = len;
	write_lock(&em_tree->lock);
	ret = btrfs_add_extent_mapping(fs_info, em_tree, &em, em->start, em->len);
	write_unlock(&em_tree->lock);
	if (ret) {
		test_err("case1 [%llu %llu]: ret %d", start, start + len, ret);
		goto out;
	}
	if (em &&
	    (em->start != 0 || extent_map_end(em) != SZ_16K ||
	     em->block_start != 0 || em->block_len != SZ_16K)) {
		test_err(
"case1 [%llu %llu]: ret %d return a wrong em (start %llu len %llu block_start %llu block_len %llu",
			 start, start + len, ret, em->start, em->len,
			 em->block_start, em->block_len);
		ret = -EINVAL;
	}
	free_extent_map(em);
out:
	free_extent_map_tree(em_tree);

	return ret;
}

/*
 * Test scenario:
 *
 * Reading the inline ending up with EEXIST, ie. read an inline
 * extent and discard page cache and read it again.
 */
static int test_case_2(struct btrfs_fs_info *fs_info,
		struct extent_map_tree *em_tree)
{
	struct extent_map *em;
	int ret;

	em = alloc_extent_map();
	if (!em) {
		test_std_err(TEST_ALLOC_EXTENT_MAP);
		return -ENOMEM;
	}

	/* Add [0, 1K) */
	em->start = 0;
	em->len = SZ_1K;
	em->block_start = EXTENT_MAP_INLINE;
	em->block_len = (u64)-1;
	write_lock(&em_tree->lock);
	ret = add_extent_mapping(em_tree, em, 0);
	write_unlock(&em_tree->lock);
	if (ret < 0) {
		test_err("cannot add extent range [0, 1K)");
		goto out;
	}
	free_extent_map(em);

	/* Add [4K, 8K) following [0, 1K)  */
	em = alloc_extent_map();
	if (!em) {
		test_std_err(TEST_ALLOC_EXTENT_MAP);
		ret = -ENOMEM;
		goto out;
	}

	em->start = SZ_4K;
	em->len = SZ_4K;
	em->block_start = SZ_4K;
	em->block_len = SZ_4K;
	write_lock(&em_tree->lock);
	ret = add_extent_mapping(em_tree, em, 0);
	write_unlock(&em_tree->lock);
	if (ret < 0) {
		test_err("cannot add extent range [4K, 8K)");
		goto out;
	}
	free_extent_map(em);

	em = alloc_extent_map();
	if (!em) {
		test_std_err(TEST_ALLOC_EXTENT_MAP);
		ret = -ENOMEM;
		goto out;
	}

	/* Add [0, 1K) */
	em->start = 0;
	em->len = SZ_1K;
	em->block_start = EXTENT_MAP_INLINE;
	em->block_len = (u64)-1;
	write_lock(&em_tree->lock);
	ret = btrfs_add_extent_mapping(fs_info, em_tree, &em, em->start, em->len);
	write_unlock(&em_tree->lock);
	if (ret) {
		test_err("case2 [0 1K]: ret %d", ret);
		goto out;
	}
	if (em &&
	    (em->start != 0 || extent_map_end(em) != SZ_1K ||
	     em->block_start != EXTENT_MAP_INLINE || em->block_len != (u64)-1)) {
		test_err(
"case2 [0 1K]: ret %d return a wrong em (start %llu len %llu block_start %llu block_len %llu",
			 ret, em->start, em->len, em->block_start,
			 em->block_len);
		ret = -EINVAL;
	}
	free_extent_map(em);
out:
	free_extent_map_tree(em_tree);

	return ret;
}

static int __test_case_3(struct btrfs_fs_info *fs_info,
		struct extent_map_tree *em_tree, u64 start)
{
	struct extent_map *em;
	u64 len = SZ_4K;
	int ret;

	em = alloc_extent_map();
	if (!em) {
		test_std_err(TEST_ALLOC_EXTENT_MAP);
		return -ENOMEM;
	}

	/* Add [4K, 8K) */
	em->start = SZ_4K;
	em->len = SZ_4K;
	em->block_start = SZ_4K;
	em->block_len = SZ_4K;
	write_lock(&em_tree->lock);
	ret = add_extent_mapping(em_tree, em, 0);
	write_unlock(&em_tree->lock);
	if (ret < 0) {
		test_err("cannot add extent range [4K, 8K)");
		goto out;
	}
	free_extent_map(em);

	em = alloc_extent_map();
	if (!em) {
		test_std_err(TEST_ALLOC_EXTENT_MAP);
		ret = -ENOMEM;
		goto out;
	}

	/* Add [0, 16K) */
	em->start = 0;
	em->len = SZ_16K;
	em->block_start = 0;
	em->block_len = SZ_16K;
	write_lock(&em_tree->lock);
	ret = btrfs_add_extent_mapping(fs_info, em_tree, &em, start, len);
	write_unlock(&em_tree->lock);
	if (ret) {
		test_err("case3 [0x%llx 0x%llx): ret %d",
			 start, start + len, ret);
		goto out;
	}
	/*
	 * Since bytes within em are contiguous, em->block_start is identical to
	 * em->start.
	 */
	if (em &&
	    (start < em->start || start + len > extent_map_end(em) ||
	     em->start != em->block_start || em->len != em->block_len)) {
		test_err(
"case3 [0x%llx 0x%llx): ret %d em (start 0x%llx len 0x%llx block_start 0x%llx block_len 0x%llx)",
			 start, start + len, ret, em->start, em->len,
			 em->block_start, em->block_len);
		ret = -EINVAL;
	}
	free_extent_map(em);
out:
	free_extent_map_tree(em_tree);

	return ret;
}

/*
 * Test scenario:
 *
 * Suppose that no extent map has been loaded into memory yet.
 * There is a file extent [0, 16K), two jobs are running concurrently
 * against it, t1 is buffered writing to [4K, 8K) and t2 is doing dio
 * read from [0, 4K) or [8K, 12K) or [12K, 16K).
 *
 * t1 goes ahead of t2 and adds em [4K, 8K) into tree.
 *
 *         t1                       t2
 *  cow_file_range()	     btrfs_get_extent()
 *                            -> lookup_extent_mapping()
 *   -> add_extent_mapping()
 *                            -> add_extent_mapping()
 */
static int test_case_3(struct btrfs_fs_info *fs_info,
		struct extent_map_tree *em_tree)
{
	int ret;

	ret = __test_case_3(fs_info, em_tree, 0);
	if (ret)
		return ret;
	ret = __test_case_3(fs_info, em_tree, SZ_8K);
	if (ret)
		return ret;
	ret = __test_case_3(fs_info, em_tree, (12 * SZ_1K));

	return ret;
}

static int __test_case_4(struct btrfs_fs_info *fs_info,
		struct extent_map_tree *em_tree, u64 start)
{
	struct extent_map *em;
	u64 len = SZ_4K;
	int ret;

	em = alloc_extent_map();
	if (!em) {
		test_std_err(TEST_ALLOC_EXTENT_MAP);
		return -ENOMEM;
	}

	/* Add [0K, 8K) */
	em->start = 0;
	em->len = SZ_8K;
	em->block_start = 0;
	em->block_len = SZ_8K;
	write_lock(&em_tree->lock);
	ret = add_extent_mapping(em_tree, em, 0);
	write_unlock(&em_tree->lock);
	if (ret < 0) {
		test_err("cannot add extent range [0, 8K)");
		goto out;
	}
	free_extent_map(em);

	em = alloc_extent_map();
	if (!em) {
		test_std_err(TEST_ALLOC_EXTENT_MAP);
		ret = -ENOMEM;
		goto out;
	}

	/* Add [8K, 32K) */
	em->start = SZ_8K;
	em->len = 24 * SZ_1K;
	em->block_start = SZ_16K; /* avoid merging */
	em->block_len = 24 * SZ_1K;
	write_lock(&em_tree->lock);
	ret = add_extent_mapping(em_tree, em, 0);
	write_unlock(&em_tree->lock);
	if (ret < 0) {
		test_err("cannot add extent range [8K, 32K)");
		goto out;
	}
	free_extent_map(em);

	em = alloc_extent_map();
	if (!em) {
		test_std_err(TEST_ALLOC_EXTENT_MAP);
		ret = -ENOMEM;
		goto out;
	}
	/* Add [0K, 32K) */
	em->start = 0;
	em->len = SZ_32K;
	em->block_start = 0;
	em->block_len = SZ_32K;
	write_lock(&em_tree->lock);
	ret = btrfs_add_extent_mapping(fs_info, em_tree, &em, start, len);
	write_unlock(&em_tree->lock);
	if (ret) {
		test_err("case4 [0x%llx 0x%llx): ret %d",
			 start, len, ret);
		goto out;
	}
	if (em && (start < em->start || start + len > extent_map_end(em))) {
		test_err(
"case4 [0x%llx 0x%llx): ret %d, added wrong em (start 0x%llx len 0x%llx block_start 0x%llx block_len 0x%llx)",
			 start, len, ret, em->start, em->len, em->block_start,
			 em->block_len);
		ret = -EINVAL;
	}
	free_extent_map(em);
out:
	free_extent_map_tree(em_tree);

	return ret;
}

/*
 * Test scenario:
 *
 * Suppose that no extent map has been loaded into memory yet.
 * There is a file extent [0, 32K), two jobs are running concurrently
 * against it, t1 is doing dio write to [8K, 32K) and t2 is doing dio
 * read from [0, 4K) or [4K, 8K).
 *
 * t1 goes ahead of t2 and splits em [0, 32K) to em [0K, 8K) and [8K 32K).
 *
 *         t1                                t2
 *  btrfs_get_blocks_direct()	       btrfs_get_blocks_direct()
 *   -> btrfs_get_extent()              -> btrfs_get_extent()
 *       -> lookup_extent_mapping()
 *       -> add_extent_mapping()            -> lookup_extent_mapping()
 *          # load [0, 32K)
 *   -> btrfs_new_extent_direct()
 *       -> btrfs_drop_extent_cache()
 *          # split [0, 32K)
 *       -> add_extent_mapping()
 *          # add [8K, 32K)
 *                                          -> add_extent_mapping()
 *                                             # handle -EEXIST when adding
 *                                             # [0, 32K)
 */
static int test_case_4(struct btrfs_fs_info *fs_info,
		struct extent_map_tree *em_tree)
{
	int ret;

	ret = __test_case_4(fs_info, em_tree, 0);
	if (ret)
		return ret;
	ret = __test_case_4(fs_info, em_tree, SZ_4K);

	return ret;
}

struct rmap_test_vector {
	u64 raid_type;
	u64 physical_start;
	u64 data_stripe_size;
	u64 num_data_stripes;
	u64 num_stripes;
	/* Assume we won't have more than 5 physical stripes */
	u64 data_stripe_phys_start[5];
	bool expected_mapped_addr;
	/* Physical to logical addresses */
	u64 mapped_logical[5];
};

static int test_rmap_block(struct btrfs_fs_info *fs_info,
			   struct rmap_test_vector *test)
{
	struct extent_map *em;
	struct map_lookup *map = NULL;
	u64 *logical = NULL;
	int i, out_ndaddrs, out_stripe_len;
	int ret;

	em = alloc_extent_map();
	if (!em) {
		test_std_err(TEST_ALLOC_EXTENT_MAP);
		return -ENOMEM;
	}

	map = kmalloc(map_lookup_size(test->num_stripes), GFP_KERNEL);
	if (!map) {
		kfree(em);
		test_std_err(TEST_ALLOC_EXTENT_MAP);
		return -ENOMEM;
	}

	set_bit(EXTENT_FLAG_FS_MAPPING, &em->flags);
	/* Start at 4GiB logical address */
	em->start = SZ_4G;
	em->len = test->data_stripe_size * test->num_data_stripes;
	em->block_len = em->len;
	em->orig_block_len = test->data_stripe_size;
	em->map_lookup = map;

	map->num_stripes = test->num_stripes;
	map->stripe_len = BTRFS_STRIPE_LEN;
	map->type = test->raid_type;

	for (i = 0; i < map->num_stripes; i++) {
		struct btrfs_device *dev = btrfs_alloc_dummy_device(fs_info);

		if (IS_ERR(dev)) {
			test_err("cannot allocate device");
			ret = PTR_ERR(dev);
			goto out;
		}
		map->stripes[i].dev = dev;
		map->stripes[i].physical = test->data_stripe_phys_start[i];
	}

	write_lock(&fs_info->mapping_tree.lock);
	ret = add_extent_mapping(&fs_info->mapping_tree, em, 0);
	write_unlock(&fs_info->mapping_tree.lock);
	if (ret) {
		test_err("error adding block group mapping to mapping tree");
		goto out_free;
	}

	ret = btrfs_rmap_block(fs_info, em->start, NULL, btrfs_sb_offset(1),
			       &logical, &out_ndaddrs, &out_stripe_len);
	if (ret || (out_ndaddrs == 0 && test->expected_mapped_addr)) {
		test_err("didn't rmap anything but expected %d",
			 test->expected_mapped_addr);
		goto out;
	}

	if (out_stripe_len != BTRFS_STRIPE_LEN) {
		test_err("calculated stripe length doesn't match");
		goto out;
	}

	if (out_ndaddrs != test->expected_mapped_addr) {
		for (i = 0; i < out_ndaddrs; i++)
			test_msg("mapped %llu", logical[i]);
		test_err("unexpected number of mapped addresses: %d", out_ndaddrs);
		goto out;
	}

	for (i = 0; i < out_ndaddrs; i++) {
		if (logical[i] != test->mapped_logical[i]) {
			test_err("unexpected logical address mapped");
			goto out;
		}
	}

	ret = 0;
out:
	write_lock(&fs_info->mapping_tree.lock);
	remove_extent_mapping(&fs_info->mapping_tree, em);
	write_unlock(&fs_info->mapping_tree.lock);
	/* For us */
	free_extent_map(em);
out_free:
	/* For the tree */
	free_extent_map(em);
	kfree(logical);
	return ret;
}

int btrfs_test_extent_map(void)
{
	struct btrfs_fs_info *fs_info = NULL;
	struct extent_map_tree *em_tree;
	int ret = 0, i;
	struct rmap_test_vector rmap_tests[] = {
		{
			/*
			 * Test a chunk with 2 data stripes one of which
			 * intersects the physical address of the super block
			 * is correctly recognised.
			 */
			.raid_type = BTRFS_BLOCK_GROUP_RAID1,
			.physical_start = SZ_64M - SZ_4M,
			.data_stripe_size = SZ_256M,
			.num_data_stripes = 2,
			.num_stripes = 2,
			.data_stripe_phys_start =
				{SZ_64M - SZ_4M, SZ_64M - SZ_4M + SZ_256M},
			.expected_mapped_addr = true,
			.mapped_logical= {SZ_4G + SZ_4M}
		},
		{
			/*
			 * Test that out-of-range physical addresses are
			 * ignored
			 */

			 /* SINGLE chunk type */
			.raid_type = 0,
			.physical_start = SZ_4G,
			.data_stripe_size = SZ_256M,
			.num_data_stripes = 1,
			.num_stripes = 1,
			.data_stripe_phys_start = {SZ_256M},
			.expected_mapped_addr = false,
			.mapped_logical = {0}
		}
	};

	test_msg("running extent_map tests");

	/*
	 * Note: the fs_info is not set up completely, we only need
	 * fs_info::fsid for the tracepoint.
	 */
	fs_info = btrfs_alloc_dummy_fs_info(PAGE_SIZE, PAGE_SIZE);
	if (!fs_info) {
		test_std_err(TEST_ALLOC_FS_INFO);
		return -ENOMEM;
	}

	em_tree = kzalloc(sizeof(*em_tree), GFP_KERNEL);
	if (!em_tree) {
		ret = -ENOMEM;
		goto out;
	}

	extent_map_tree_init(em_tree);

	ret = test_case_1(fs_info, em_tree);
	if (ret)
		goto out;
	ret = test_case_2(fs_info, em_tree);
	if (ret)
		goto out;
	ret = test_case_3(fs_info, em_tree);
	if (ret)
		goto out;
	ret = test_case_4(fs_info, em_tree);

	test_msg("running rmap tests");
	for (i = 0; i < ARRAY_SIZE(rmap_tests); i++) {
		ret = test_rmap_block(fs_info, &rmap_tests[i]);
		if (ret)
			goto out;
	}

out:
	kfree(em_tree);
	btrfs_free_dummy_fs_info(fs_info);

	return ret;
}
