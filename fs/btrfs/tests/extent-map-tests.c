// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2017 Oracle.  All rights reserved.
 */

#include <linux/types.h>
#include "btrfs-tests.h"
#include "../ctree.h"

static void free_extent_map_tree(struct extent_map_tree *em_tree)
{
	struct extent_map *em;
	struct rb_node *node;

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
	ret = add_extent_mapping(em_tree, em, 0);
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
	ret = add_extent_mapping(em_tree, em, 0);
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
	ret = btrfs_add_extent_mapping(fs_info, em_tree, &em, em->start, em->len);
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
	ret = add_extent_mapping(em_tree, em, 0);
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
	ret = add_extent_mapping(em_tree, em, 0);
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
	ret = btrfs_add_extent_mapping(fs_info, em_tree, &em, em->start, em->len);
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
	ret = add_extent_mapping(em_tree, em, 0);
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
	ret = btrfs_add_extent_mapping(fs_info, em_tree, &em, start, len);
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
	ret = add_extent_mapping(em_tree, em, 0);
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
	ret = add_extent_mapping(em_tree, em, 0);
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
	ret = btrfs_add_extent_mapping(fs_info, em_tree, &em, start, len);
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

int btrfs_test_extent_map(void)
{
	struct btrfs_fs_info *fs_info = NULL;
	struct extent_map_tree *em_tree;
	int ret = 0;

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

out:
	kfree(em_tree);
	btrfs_free_dummy_fs_info(fs_info);

	return ret;
}
