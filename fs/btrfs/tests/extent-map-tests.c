// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2017 Oracle.  All rights reserved.
 */

#include <linux/types.h>
#include "btrfs-tests.h"
#include "../ctree.h"
#include "../btrfs_inode.h"
#include "../volumes.h"
#include "../disk-io.h"
#include "../block-group.h"

static int free_extent_map_tree(struct btrfs_inode *inode)
{
	struct extent_map_tree *em_tree = &inode->extent_tree;
	struct extent_map *em;
	struct rb_node *node;
	int ret = 0;

	write_lock(&em_tree->lock);
	while (!RB_EMPTY_ROOT(&em_tree->root)) {
		node = rb_first(&em_tree->root);
		em = rb_entry(node, struct extent_map, rb_node);
		btrfs_remove_extent_mapping(inode, em);

#ifdef CONFIG_BTRFS_DEBUG
		if (refcount_read(&em->refs) != 1) {
			ret = -EINVAL;
			test_err(
"em leak: em (start %llu len %llu disk_bytenr %llu disk_num_bytes %llu offset %llu) refs %d",
				 em->start, em->len, em->disk_bytenr,
				 em->disk_num_bytes, em->offset,
				 refcount_read(&em->refs));

			refcount_set(&em->refs, 1);
		}
#endif
		btrfs_free_extent_map(em);
	}
	write_unlock(&em_tree->lock);

	return ret;
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
static int test_case_1(struct btrfs_fs_info *fs_info, struct btrfs_inode *inode)
{
	struct extent_map_tree *em_tree = &inode->extent_tree;
	struct extent_map *em;
	u64 start = 0;
	u64 len = SZ_8K;
	int ret;
	int ret2;

	em = btrfs_alloc_extent_map();
	if (!em) {
		test_std_err(TEST_ALLOC_EXTENT_MAP);
		return -ENOMEM;
	}

	/* Add [0, 16K) */
	em->start = 0;
	em->len = SZ_16K;
	em->disk_bytenr = 0;
	em->disk_num_bytes = SZ_16K;
	em->ram_bytes = SZ_16K;
	write_lock(&em_tree->lock);
	ret = btrfs_add_extent_mapping(inode, &em, em->start, em->len);
	write_unlock(&em_tree->lock);
	if (ret < 0) {
		test_err("cannot add extent range [0, 16K)");
		goto out;
	}
	btrfs_free_extent_map(em);

	/* Add [16K, 20K) following [0, 16K)  */
	em = btrfs_alloc_extent_map();
	if (!em) {
		test_std_err(TEST_ALLOC_EXTENT_MAP);
		ret = -ENOMEM;
		goto out;
	}

	em->start = SZ_16K;
	em->len = SZ_4K;
	em->disk_bytenr = SZ_32K; /* avoid merging */
	em->disk_num_bytes = SZ_4K;
	em->ram_bytes = SZ_4K;
	write_lock(&em_tree->lock);
	ret = btrfs_add_extent_mapping(inode, &em, em->start, em->len);
	write_unlock(&em_tree->lock);
	if (ret < 0) {
		test_err("cannot add extent range [16K, 20K)");
		goto out;
	}
	btrfs_free_extent_map(em);

	em = btrfs_alloc_extent_map();
	if (!em) {
		test_std_err(TEST_ALLOC_EXTENT_MAP);
		ret = -ENOMEM;
		goto out;
	}

	/* Add [0, 8K), should return [0, 16K) instead. */
	em->start = start;
	em->len = len;
	em->disk_bytenr = start;
	em->disk_num_bytes = len;
	em->ram_bytes = len;
	write_lock(&em_tree->lock);
	ret = btrfs_add_extent_mapping(inode, &em, em->start, em->len);
	write_unlock(&em_tree->lock);
	if (ret) {
		test_err("case1 [%llu %llu]: ret %d", start, start + len, ret);
		goto out;
	}
	if (!em) {
		test_err("case1 [%llu %llu]: no extent map returned",
			 start, start + len);
		ret = -ENOENT;
		goto out;
	}
	if (em->start != 0 || btrfs_extent_map_end(em) != SZ_16K ||
	    em->disk_bytenr != 0 || em->disk_num_bytes != SZ_16K) {
		test_err(
"case1 [%llu %llu]: ret %d return a wrong em (start %llu len %llu disk_bytenr %llu disk_num_bytes %llu",
			 start, start + len, ret, em->start, em->len,
			 em->disk_bytenr, em->disk_num_bytes);
		ret = -EINVAL;
	}
	btrfs_free_extent_map(em);
out:
	ret2 = free_extent_map_tree(inode);
	if (ret == 0)
		ret = ret2;

	return ret;
}

/*
 * Test scenario:
 *
 * Reading the inline ending up with EEXIST, ie. read an inline
 * extent and discard page cache and read it again.
 */
static int test_case_2(struct btrfs_fs_info *fs_info, struct btrfs_inode *inode)
{
	struct extent_map_tree *em_tree = &inode->extent_tree;
	struct extent_map *em;
	int ret;
	int ret2;

	em = btrfs_alloc_extent_map();
	if (!em) {
		test_std_err(TEST_ALLOC_EXTENT_MAP);
		return -ENOMEM;
	}

	/* Add [0, 1K) */
	em->start = 0;
	em->len = SZ_1K;
	em->disk_bytenr = EXTENT_MAP_INLINE;
	em->disk_num_bytes = 0;
	em->ram_bytes = SZ_1K;
	write_lock(&em_tree->lock);
	ret = btrfs_add_extent_mapping(inode, &em, em->start, em->len);
	write_unlock(&em_tree->lock);
	if (ret < 0) {
		test_err("cannot add extent range [0, 1K)");
		goto out;
	}
	btrfs_free_extent_map(em);

	/* Add [4K, 8K) following [0, 1K)  */
	em = btrfs_alloc_extent_map();
	if (!em) {
		test_std_err(TEST_ALLOC_EXTENT_MAP);
		ret = -ENOMEM;
		goto out;
	}

	em->start = SZ_4K;
	em->len = SZ_4K;
	em->disk_bytenr = SZ_4K;
	em->disk_num_bytes = SZ_4K;
	em->ram_bytes = SZ_4K;
	write_lock(&em_tree->lock);
	ret = btrfs_add_extent_mapping(inode, &em, em->start, em->len);
	write_unlock(&em_tree->lock);
	if (ret < 0) {
		test_err("cannot add extent range [4K, 8K)");
		goto out;
	}
	btrfs_free_extent_map(em);

	em = btrfs_alloc_extent_map();
	if (!em) {
		test_std_err(TEST_ALLOC_EXTENT_MAP);
		ret = -ENOMEM;
		goto out;
	}

	/* Add [0, 1K) */
	em->start = 0;
	em->len = SZ_1K;
	em->disk_bytenr = EXTENT_MAP_INLINE;
	em->disk_num_bytes = 0;
	em->ram_bytes = SZ_1K;
	write_lock(&em_tree->lock);
	ret = btrfs_add_extent_mapping(inode, &em, em->start, em->len);
	write_unlock(&em_tree->lock);
	if (ret) {
		test_err("case2 [0 1K]: ret %d", ret);
		goto out;
	}
	if (!em) {
		test_err("case2 [0 1K]: no extent map returned");
		ret = -ENOENT;
		goto out;
	}
	if (em->start != 0 || btrfs_extent_map_end(em) != SZ_1K ||
	    em->disk_bytenr != EXTENT_MAP_INLINE) {
		test_err(
"case2 [0 1K]: ret %d return a wrong em (start %llu len %llu disk_bytenr %llu",
			 ret, em->start, em->len, em->disk_bytenr);
		ret = -EINVAL;
	}
	btrfs_free_extent_map(em);
out:
	ret2 = free_extent_map_tree(inode);
	if (ret == 0)
		ret = ret2;

	return ret;
}

static int __test_case_3(struct btrfs_fs_info *fs_info,
			 struct btrfs_inode *inode, u64 start)
{
	struct extent_map_tree *em_tree = &inode->extent_tree;
	struct extent_map *em;
	u64 len = SZ_4K;
	int ret;
	int ret2;

	em = btrfs_alloc_extent_map();
	if (!em) {
		test_std_err(TEST_ALLOC_EXTENT_MAP);
		return -ENOMEM;
	}

	/* Add [4K, 8K) */
	em->start = SZ_4K;
	em->len = SZ_4K;
	em->disk_bytenr = SZ_4K;
	em->disk_num_bytes = SZ_4K;
	em->ram_bytes = SZ_4K;
	write_lock(&em_tree->lock);
	ret = btrfs_add_extent_mapping(inode, &em, em->start, em->len);
	write_unlock(&em_tree->lock);
	if (ret < 0) {
		test_err("cannot add extent range [4K, 8K)");
		goto out;
	}
	btrfs_free_extent_map(em);

	em = btrfs_alloc_extent_map();
	if (!em) {
		test_std_err(TEST_ALLOC_EXTENT_MAP);
		ret = -ENOMEM;
		goto out;
	}

	/* Add [0, 16K) */
	em->start = 0;
	em->len = SZ_16K;
	em->disk_bytenr = 0;
	em->disk_num_bytes = SZ_16K;
	em->ram_bytes = SZ_16K;
	write_lock(&em_tree->lock);
	ret = btrfs_add_extent_mapping(inode, &em, start, len);
	write_unlock(&em_tree->lock);
	if (ret) {
		test_err("case3 [%llu %llu): ret %d",
			 start, start + len, ret);
		goto out;
	}
	if (!em) {
		test_err("case3 [%llu %llu): no extent map returned",
			 start, start + len);
		ret = -ENOENT;
		goto out;
	}
	/*
	 * Since bytes within em are contiguous, em->block_start is identical to
	 * em->start.
	 */
	if (start < em->start || start + len > btrfs_extent_map_end(em) ||
	    em->start != btrfs_extent_map_block_start(em)) {
		test_err(
"case3 [%llu %llu): ret %d em (start %llu len %llu disk_bytenr %llu block_len %llu)",
			 start, start + len, ret, em->start, em->len,
			 em->disk_bytenr, em->disk_num_bytes);
		ret = -EINVAL;
	}
	btrfs_free_extent_map(em);
out:
	ret2 = free_extent_map_tree(inode);
	if (ret == 0)
		ret = ret2;

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
static int test_case_3(struct btrfs_fs_info *fs_info, struct btrfs_inode *inode)
{
	int ret;

	ret = __test_case_3(fs_info, inode, 0);
	if (ret)
		return ret;
	ret = __test_case_3(fs_info, inode, SZ_8K);
	if (ret)
		return ret;
	ret = __test_case_3(fs_info, inode, (12 * SZ_1K));

	return ret;
}

static int __test_case_4(struct btrfs_fs_info *fs_info,
			 struct btrfs_inode *inode, u64 start)
{
	struct extent_map_tree *em_tree = &inode->extent_tree;
	struct extent_map *em;
	u64 len = SZ_4K;
	int ret;
	int ret2;

	em = btrfs_alloc_extent_map();
	if (!em) {
		test_std_err(TEST_ALLOC_EXTENT_MAP);
		return -ENOMEM;
	}

	/* Add [0K, 8K) */
	em->start = 0;
	em->len = SZ_8K;
	em->disk_bytenr = 0;
	em->disk_num_bytes = SZ_8K;
	em->ram_bytes = SZ_8K;
	write_lock(&em_tree->lock);
	ret = btrfs_add_extent_mapping(inode, &em, em->start, em->len);
	write_unlock(&em_tree->lock);
	if (ret < 0) {
		test_err("cannot add extent range [0, 8K)");
		goto out;
	}
	btrfs_free_extent_map(em);

	em = btrfs_alloc_extent_map();
	if (!em) {
		test_std_err(TEST_ALLOC_EXTENT_MAP);
		ret = -ENOMEM;
		goto out;
	}

	/* Add [8K, 32K) */
	em->start = SZ_8K;
	em->len = 24 * SZ_1K;
	em->disk_bytenr = SZ_16K; /* avoid merging */
	em->disk_num_bytes = 24 * SZ_1K;
	em->ram_bytes = 24 * SZ_1K;
	write_lock(&em_tree->lock);
	ret = btrfs_add_extent_mapping(inode, &em, em->start, em->len);
	write_unlock(&em_tree->lock);
	if (ret < 0) {
		test_err("cannot add extent range [8K, 32K)");
		goto out;
	}
	btrfs_free_extent_map(em);

	em = btrfs_alloc_extent_map();
	if (!em) {
		test_std_err(TEST_ALLOC_EXTENT_MAP);
		ret = -ENOMEM;
		goto out;
	}
	/* Add [0K, 32K) */
	em->start = 0;
	em->len = SZ_32K;
	em->disk_bytenr = 0;
	em->disk_num_bytes = SZ_32K;
	em->ram_bytes = SZ_32K;
	write_lock(&em_tree->lock);
	ret = btrfs_add_extent_mapping(inode, &em, start, len);
	write_unlock(&em_tree->lock);
	if (ret) {
		test_err("case4 [%llu %llu): ret %d",
			 start, start + len, ret);
		goto out;
	}
	if (!em) {
		test_err("case4 [%llu %llu): no extent map returned",
			 start, start + len);
		ret = -ENOENT;
		goto out;
	}
	if (start < em->start || start + len > btrfs_extent_map_end(em)) {
		test_err(
"case4 [%llu %llu): ret %d, added wrong em (start %llu len %llu disk_bytenr %llu disk_num_bytes %llu)",
			 start, start + len, ret, em->start, em->len,
			 em->disk_bytenr, em->disk_num_bytes);
		ret = -EINVAL;
	}
	btrfs_free_extent_map(em);
out:
	ret2 = free_extent_map_tree(inode);
	if (ret == 0)
		ret = ret2;

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
static int test_case_4(struct btrfs_fs_info *fs_info, struct btrfs_inode *inode)
{
	int ret;

	ret = __test_case_4(fs_info, inode, 0);
	if (ret)
		return ret;
	ret = __test_case_4(fs_info, inode, SZ_4K);

	return ret;
}

static int add_compressed_extent(struct btrfs_inode *inode,
				 u64 start, u64 len, u64 block_start)
{
	struct extent_map_tree *em_tree = &inode->extent_tree;
	struct extent_map *em;
	int ret;

	em = btrfs_alloc_extent_map();
	if (!em) {
		test_std_err(TEST_ALLOC_EXTENT_MAP);
		return -ENOMEM;
	}

	em->start = start;
	em->len = len;
	em->disk_bytenr = block_start;
	em->disk_num_bytes = SZ_4K;
	em->ram_bytes = len;
	em->flags |= EXTENT_FLAG_COMPRESS_ZLIB;
	write_lock(&em_tree->lock);
	ret = btrfs_add_extent_mapping(inode, &em, em->start, em->len);
	write_unlock(&em_tree->lock);
	btrfs_free_extent_map(em);
	if (ret < 0) {
		test_err("cannot add extent map [%llu, %llu)", start, start + len);
		return ret;
	}

	return 0;
}

struct extent_range {
	u64 start;
	u64 len;
};

/* The valid states of the tree after every drop, as described below. */
struct extent_range valid_ranges[][7] = {
	{
	  { .start = 0,			.len = SZ_8K },		/* [0, 8K) */
	  { .start = SZ_4K * 3,		.len = SZ_4K * 3},	/* [12k, 24k) */
	  { .start = SZ_4K * 6,		.len = SZ_4K * 3},	/* [24k, 36k) */
	  { .start = SZ_32K + SZ_4K,	.len = SZ_4K},		/* [36k, 40k) */
	  { .start = SZ_4K * 10,	.len = SZ_4K * 6},	/* [40k, 64k) */
	},
	{
	  { .start = 0,			.len = SZ_8K },		/* [0, 8K) */
	  { .start = SZ_4K * 5,		.len = SZ_4K},		/* [20k, 24k) */
	  { .start = SZ_4K * 6,		.len = SZ_4K * 3},	/* [24k, 36k) */
	  { .start = SZ_32K + SZ_4K,	.len = SZ_4K},		/* [36k, 40k) */
	  { .start = SZ_4K * 10,	.len = SZ_4K * 6},	/* [40k, 64k) */
	},
	{
	  { .start = 0,			.len = SZ_8K },		/* [0, 8K) */
	  { .start = SZ_4K * 5,		.len = SZ_4K},		/* [20k, 24k) */
	  { .start = SZ_4K * 6,		.len = SZ_4K},		/* [24k, 28k) */
	  { .start = SZ_32K,		.len = SZ_4K},		/* [32k, 36k) */
	  { .start = SZ_32K + SZ_4K,	.len = SZ_4K},		/* [36k, 40k) */
	  { .start = SZ_4K * 10,	.len = SZ_4K * 6},	/* [40k, 64k) */
	},
	{
	  { .start = 0,			.len = SZ_8K},		/* [0, 8K) */
	  { .start = SZ_4K * 5,		.len = SZ_4K},		/* [20k, 24k) */
	  { .start = SZ_4K * 6,		.len = SZ_4K},		/* [24k, 28k) */
	}
};

static int validate_range(struct extent_map_tree *em_tree, int index)
{
	struct rb_node *n;
	int i;

	for (i = 0, n = rb_first(&em_tree->root);
	     valid_ranges[index][i].len && n;
	     i++, n = rb_next(n)) {
		struct extent_map *entry = rb_entry(n, struct extent_map, rb_node);

		if (entry->start != valid_ranges[index][i].start) {
			test_err("mapping has start %llu expected %llu",
				 entry->start, valid_ranges[index][i].start);
			return -EINVAL;
		}

		if (entry->len != valid_ranges[index][i].len) {
			test_err("mapping has len %llu expected %llu",
				 entry->len, valid_ranges[index][i].len);
			return -EINVAL;
		}
	}

	/*
	 * We exited because we don't have any more entries in the extent_map
	 * but we still expect more valid entries.
	 */
	if (valid_ranges[index][i].len) {
		test_err("missing an entry");
		return -EINVAL;
	}

	/* We exited the loop but still have entries in the extent map. */
	if (n) {
		test_err("we have a left over entry in the extent map we didn't expect");
		return -EINVAL;
	}

	return 0;
}

/*
 * Test scenario:
 *
 * Test the various edge cases of btrfs_drop_extent_map_range, create the
 * following ranges
 *
 * [0, 12k)[12k, 24k)[24k, 36k)[36k, 40k)[40k,64k)
 *
 * And then we'll drop:
 *
 * [8k, 12k) - test the single front split
 * [12k, 20k) - test the single back split
 * [28k, 32k) - test the double split
 * [32k, 64k) - test whole em dropping
 *
 * They'll have the EXTENT_FLAG_COMPRESSED flag set to keep the em tree from
 * merging the em's.
 */
static int test_case_5(struct btrfs_fs_info *fs_info, struct btrfs_inode *inode)
{
	u64 start, end;
	int ret;
	int ret2;

	test_msg("Running btrfs_drop_extent_map_range tests");

	/* [0, 12k) */
	ret = add_compressed_extent(inode, 0, SZ_4K * 3, 0);
	if (ret) {
		test_err("cannot add extent range [0, 12K)");
		goto out;
	}

	/* [12k, 24k) */
	ret = add_compressed_extent(inode, SZ_4K * 3, SZ_4K * 3, SZ_4K);
	if (ret) {
		test_err("cannot add extent range [12k, 24k)");
		goto out;
	}

	/* [24k, 36k) */
	ret = add_compressed_extent(inode, SZ_4K * 6, SZ_4K * 3, SZ_8K);
	if (ret) {
		test_err("cannot add extent range [12k, 24k)");
		goto out;
	}

	/* [36k, 40k) */
	ret = add_compressed_extent(inode, SZ_32K + SZ_4K, SZ_4K, SZ_4K * 3);
	if (ret) {
		test_err("cannot add extent range [12k, 24k)");
		goto out;
	}

	/* [40k, 64k) */
	ret = add_compressed_extent(inode, SZ_4K * 10, SZ_4K * 6, SZ_16K);
	if (ret) {
		test_err("cannot add extent range [12k, 24k)");
		goto out;
	}

	/* Drop [8k, 12k) */
	start = SZ_8K;
	end = (3 * SZ_4K) - 1;
	btrfs_drop_extent_map_range(inode, start, end, false);
	ret = validate_range(&inode->extent_tree, 0);
	if (ret)
		goto out;

	/* Drop [12k, 20k) */
	start = SZ_4K * 3;
	end = SZ_16K + SZ_4K - 1;
	btrfs_drop_extent_map_range(inode, start, end, false);
	ret = validate_range(&inode->extent_tree, 1);
	if (ret)
		goto out;

	/* Drop [28k, 32k) */
	start = SZ_32K - SZ_4K;
	end = SZ_32K - 1;
	btrfs_drop_extent_map_range(inode, start, end, false);
	ret = validate_range(&inode->extent_tree, 2);
	if (ret)
		goto out;

	/* Drop [32k, 64k) */
	start = SZ_32K;
	end = SZ_64K - 1;
	btrfs_drop_extent_map_range(inode, start, end, false);
	ret = validate_range(&inode->extent_tree, 3);
	if (ret)
		goto out;
out:
	ret2 = free_extent_map_tree(inode);
	if (ret == 0)
		ret = ret2;

	return ret;
}

/*
 * Test the btrfs_add_extent_mapping helper which will attempt to create an em
 * for areas between two existing ems.  Validate it doesn't do this when there
 * are two unmerged em's side by side.
 */
static int test_case_6(struct btrfs_fs_info *fs_info, struct btrfs_inode *inode)
{
	struct extent_map_tree *em_tree = &inode->extent_tree;
	struct extent_map *em = NULL;
	int ret;
	int ret2;

	ret = add_compressed_extent(inode, 0, SZ_4K, 0);
	if (ret)
		goto out;

	ret = add_compressed_extent(inode, SZ_4K, SZ_4K, 0);
	if (ret)
		goto out;

	em = btrfs_alloc_extent_map();
	if (!em) {
		test_std_err(TEST_ALLOC_EXTENT_MAP);
		ret = -ENOMEM;
		goto out;
	}

	em->start = SZ_4K;
	em->len = SZ_4K;
	em->disk_bytenr = SZ_16K;
	em->disk_num_bytes = SZ_16K;
	em->ram_bytes = SZ_16K;
	write_lock(&em_tree->lock);
	ret = btrfs_add_extent_mapping(inode, &em, 0, SZ_8K);
	write_unlock(&em_tree->lock);

	if (ret != 0) {
		test_err("got an error when adding our em: %d", ret);
		goto out;
	}

	ret = -EINVAL;
	if (em->start != 0) {
		test_err("unexpected em->start at %llu, wanted 0", em->start);
		goto out;
	}
	if (em->len != SZ_4K) {
		test_err("unexpected em->len %llu, expected 4K", em->len);
		goto out;
	}
	ret = 0;
out:
	btrfs_free_extent_map(em);
	ret2 = free_extent_map_tree(inode);
	if (ret == 0)
		ret = ret2;

	return ret;
}

/*
 * Regression test for btrfs_drop_extent_map_range.  Calling with skip_pinned ==
 * true would mess up the start/end calculations and subsequent splits would be
 * incorrect.
 */
static int test_case_7(struct btrfs_fs_info *fs_info, struct btrfs_inode *inode)
{
	struct extent_map_tree *em_tree = &inode->extent_tree;
	struct extent_map *em;
	int ret;
	int ret2;

	test_msg("Running btrfs_drop_extent_cache with pinned");

	em = btrfs_alloc_extent_map();
	if (!em) {
		test_std_err(TEST_ALLOC_EXTENT_MAP);
		return -ENOMEM;
	}

	/* [0, 16K), pinned */
	em->start = 0;
	em->len = SZ_16K;
	em->disk_bytenr = 0;
	em->disk_num_bytes = SZ_4K;
	em->ram_bytes = SZ_16K;
	em->flags |= (EXTENT_FLAG_PINNED | EXTENT_FLAG_COMPRESS_ZLIB);
	write_lock(&em_tree->lock);
	ret = btrfs_add_extent_mapping(inode, &em, em->start, em->len);
	write_unlock(&em_tree->lock);
	if (ret < 0) {
		test_err("couldn't add extent map");
		goto out;
	}
	btrfs_free_extent_map(em);

	em = btrfs_alloc_extent_map();
	if (!em) {
		test_std_err(TEST_ALLOC_EXTENT_MAP);
		ret = -ENOMEM;
		goto out;
	}

	/* [32K, 48K), not pinned */
	em->start = SZ_32K;
	em->len = SZ_16K;
	em->disk_bytenr = SZ_32K;
	em->disk_num_bytes = SZ_16K;
	em->ram_bytes = SZ_16K;
	write_lock(&em_tree->lock);
	ret = btrfs_add_extent_mapping(inode, &em, em->start, em->len);
	write_unlock(&em_tree->lock);
	if (ret < 0) {
		test_err("couldn't add extent map");
		goto out;
	}
	btrfs_free_extent_map(em);

	/*
	 * Drop [0, 36K) This should skip the [0, 4K) extent and then split the
	 * [32K, 48K) extent.
	 */
	btrfs_drop_extent_map_range(inode, 0, (36 * SZ_1K) - 1, true);

	/* Make sure our extent maps look sane. */
	ret = -EINVAL;

	em = btrfs_lookup_extent_mapping(em_tree, 0, SZ_16K);
	if (!em) {
		test_err("didn't find an em at 0 as expected");
		goto out;
	}

	if (em->start != 0) {
		test_err("em->start is %llu, expected 0", em->start);
		goto out;
	}

	if (em->len != SZ_16K) {
		test_err("em->len is %llu, expected 16K", em->len);
		goto out;
	}

	btrfs_free_extent_map(em);

	read_lock(&em_tree->lock);
	em = btrfs_lookup_extent_mapping(em_tree, SZ_16K, SZ_16K);
	read_unlock(&em_tree->lock);
	if (em) {
		test_err("found an em when we weren't expecting one");
		goto out;
	}

	read_lock(&em_tree->lock);
	em = btrfs_lookup_extent_mapping(em_tree, SZ_32K, SZ_16K);
	read_unlock(&em_tree->lock);
	if (!em) {
		test_err("didn't find an em at 32K as expected");
		goto out;
	}

	if (em->start != (36 * SZ_1K)) {
		test_err("em->start is %llu, expected 36K", em->start);
		goto out;
	}

	if (em->len != (12 * SZ_1K)) {
		test_err("em->len is %llu, expected 12K", em->len);
		goto out;
	}

	if (btrfs_extent_map_block_start(em) != SZ_32K + SZ_4K) {
		test_err("em->block_start is %llu, expected 36K",
			 btrfs_extent_map_block_start(em));
		goto out;
	}

	btrfs_free_extent_map(em);

	read_lock(&em_tree->lock);
	em = btrfs_lookup_extent_mapping(em_tree, 48 * SZ_1K, (u64)-1);
	read_unlock(&em_tree->lock);
	if (em) {
		test_err("found an unexpected em above 48K");
		goto out;
	}

	ret = 0;
out:
	btrfs_free_extent_map(em);
	/* Unpin our extent to prevent warning when removing it below. */
	ret2 = btrfs_unpin_extent_cache(inode, 0, SZ_16K, 0);
	if (ret == 0)
		ret = ret2;
	ret2 = free_extent_map_tree(inode);
	if (ret == 0)
		ret = ret2;

	return ret;
}

/*
 * Test a regression for compressed extent map adjustment when we attempt to
 * add an extent map that is partially overlapped by another existing extent
 * map. The resulting extent map offset was left unchanged despite having
 * incremented its start offset.
 */
static int test_case_8(struct btrfs_fs_info *fs_info, struct btrfs_inode *inode)
{
	struct extent_map_tree *em_tree = &inode->extent_tree;
	struct extent_map *em;
	int ret;
	int ret2;

	em = btrfs_alloc_extent_map();
	if (!em) {
		test_std_err(TEST_ALLOC_EXTENT_MAP);
		return -ENOMEM;
	}

	/* Compressed extent for the file range [120K, 128K). */
	em->start = SZ_1K * 120;
	em->len = SZ_8K;
	em->disk_num_bytes = SZ_4K;
	em->ram_bytes = SZ_8K;
	em->flags |= EXTENT_FLAG_COMPRESS_ZLIB;
	write_lock(&em_tree->lock);
	ret = btrfs_add_extent_mapping(inode, &em, em->start, em->len);
	write_unlock(&em_tree->lock);
	btrfs_free_extent_map(em);
	if (ret < 0) {
		test_err("couldn't add extent map for range [120K, 128K)");
		goto out;
	}

	em = btrfs_alloc_extent_map();
	if (!em) {
		test_std_err(TEST_ALLOC_EXTENT_MAP);
		ret = -ENOMEM;
		goto out;
	}

	/*
	 * Compressed extent for the file range [108K, 144K), which overlaps
	 * with the [120K, 128K) we previously inserted.
	 */
	em->start = SZ_1K * 108;
	em->len = SZ_1K * 36;
	em->disk_num_bytes = SZ_4K;
	em->ram_bytes = SZ_1K * 36;
	em->flags |= EXTENT_FLAG_COMPRESS_ZLIB;

	/*
	 * Try to add the extent map but with a search range of [140K, 144K),
	 * this should succeed and adjust the extent map to the range
	 * [128K, 144K), with a length of 16K and an offset of 20K.
	 *
	 * This simulates a scenario where in the subvolume tree of an inode we
	 * have a compressed file extent item for the range [108K, 144K) and we
	 * have an overlapping compressed extent map for the range [120K, 128K),
	 * which was created by an encoded write, but its ordered extent was not
	 * yet completed, so the subvolume tree doesn't have yet the file extent
	 * item for that range - we only have the extent map in the inode's
	 * extent map tree.
	 */
	write_lock(&em_tree->lock);
	ret = btrfs_add_extent_mapping(inode, &em, SZ_1K * 140, SZ_4K);
	write_unlock(&em_tree->lock);
	btrfs_free_extent_map(em);
	if (ret < 0) {
		test_err("couldn't add extent map for range [108K, 144K)");
		goto out;
	}

	if (em->start != SZ_128K) {
		test_err("unexpected extent map start %llu (should be 128K)", em->start);
		ret = -EINVAL;
		goto out;
	}
	if (em->len != SZ_16K) {
		test_err("unexpected extent map length %llu (should be 16K)", em->len);
		ret = -EINVAL;
		goto out;
	}
	if (em->offset != SZ_1K * 20) {
		test_err("unexpected extent map offset %llu (should be 20K)", em->offset);
		ret = -EINVAL;
		goto out;
	}
out:
	ret2 = free_extent_map_tree(inode);
	if (ret == 0)
		ret = ret2;

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
	struct btrfs_chunk_map *map;
	u64 *logical = NULL;
	int i, out_ndaddrs, out_stripe_len;
	int ret;

	map = btrfs_alloc_chunk_map(test->num_stripes, GFP_KERNEL);
	if (!map) {
		test_std_err(TEST_ALLOC_CHUNK_MAP);
		return -ENOMEM;
	}

	/* Start at 4GiB logical address */
	map->start = SZ_4G;
	map->chunk_len = test->data_stripe_size * test->num_data_stripes;
	map->stripe_size = test->data_stripe_size;
	map->num_stripes = test->num_stripes;
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

	ret = btrfs_add_chunk_map(fs_info, map);
	if (ret) {
		test_err("error adding chunk map to mapping tree");
		btrfs_free_chunk_map(map);
		goto out_free;
	}

	ret = btrfs_rmap_block(fs_info, map->start, btrfs_sb_offset(1),
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
	btrfs_remove_chunk_map(fs_info, map);
out_free:
	kfree(logical);
	return ret;
}

int btrfs_test_extent_map(void)
{
	struct btrfs_fs_info *fs_info = NULL;
	struct inode *inode;
	struct btrfs_root *root = NULL;
	int ret = 0, i;
	struct rmap_test_vector rmap_tests[] = {
		{
			/*
			 * Test a chunk with 2 data stripes one of which
			 * intersects the physical address of the super block
			 * is correctly recognized.
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

	inode = btrfs_new_test_inode();
	if (!inode) {
		test_std_err(TEST_ALLOC_INODE);
		ret = -ENOMEM;
		goto out;
	}

	root = btrfs_alloc_dummy_root(fs_info);
	if (IS_ERR(root)) {
		test_std_err(TEST_ALLOC_ROOT);
		ret = PTR_ERR(root);
		root = NULL;
		goto out;
	}

	BTRFS_I(inode)->root = root;

	ret = test_case_1(fs_info, BTRFS_I(inode));
	if (ret)
		goto out;
	ret = test_case_2(fs_info, BTRFS_I(inode));
	if (ret)
		goto out;
	ret = test_case_3(fs_info, BTRFS_I(inode));
	if (ret)
		goto out;
	ret = test_case_4(fs_info, BTRFS_I(inode));
	if (ret)
		goto out;
	ret = test_case_5(fs_info, BTRFS_I(inode));
	if (ret)
		goto out;
	ret = test_case_6(fs_info, BTRFS_I(inode));
	if (ret)
		goto out;
	ret = test_case_7(fs_info, BTRFS_I(inode));
	if (ret)
		goto out;
	ret = test_case_8(fs_info, BTRFS_I(inode));
	if (ret)
		goto out;

	test_msg("running rmap tests");
	for (i = 0; i < ARRAY_SIZE(rmap_tests); i++) {
		ret = test_rmap_block(fs_info, &rmap_tests[i]);
		if (ret)
			goto out;
	}

out:
	iput(inode);
	btrfs_free_dummy_root(root);
	btrfs_free_dummy_fs_info(fs_info);

	return ret;
}
