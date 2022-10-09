// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2013 Fusion IO.  All rights reserved.
 */

#include <linux/pagemap.h>
#include <linux/sched.h>
#include <linux/slab.h>
#include <linux/sizes.h>
#include "btrfs-tests.h"
#include "../ctree.h"
#include "../extent_io.h"
#include "../btrfs_inode.h"

#define PROCESS_UNLOCK		(1 << 0)
#define PROCESS_RELEASE		(1 << 1)
#define PROCESS_TEST_LOCKED	(1 << 2)

static noinline int process_page_range(struct inode *inode, u64 start, u64 end,
				       unsigned long flags)
{
	int ret;
	struct page *pages[16];
	unsigned long index = start >> PAGE_SHIFT;
	unsigned long end_index = end >> PAGE_SHIFT;
	unsigned long nr_pages = end_index - index + 1;
	int i;
	int count = 0;
	int loops = 0;

	while (nr_pages > 0) {
		ret = find_get_pages_contig(inode->i_mapping, index,
				     min_t(unsigned long, nr_pages,
				     ARRAY_SIZE(pages)), pages);
		for (i = 0; i < ret; i++) {
			if (flags & PROCESS_TEST_LOCKED &&
			    !PageLocked(pages[i]))
				count++;
			if (flags & PROCESS_UNLOCK && PageLocked(pages[i]))
				unlock_page(pages[i]);
			put_page(pages[i]);
			if (flags & PROCESS_RELEASE)
				put_page(pages[i]);
		}
		nr_pages -= ret;
		index += ret;
		cond_resched();
		loops++;
		if (loops > 100000) {
			printk(KERN_ERR
		"stuck in a loop, start %llu, end %llu, nr_pages %lu, ret %d\n",
				start, end, nr_pages, ret);
			break;
		}
	}
	return count;
}

#define STATE_FLAG_STR_LEN			256

#define PRINT_ONE_FLAG(state, dest, cur, name)				\
({									\
	if (state->state & EXTENT_##name)				\
		cur += scnprintf(dest + cur, STATE_FLAG_STR_LEN - cur,	\
				 "%s" #name, cur == 0 ? "" : "|");	\
})

static void extent_flag_to_str(const struct extent_state *state, char *dest)
{
	int cur = 0;

	dest[0] = 0;
	PRINT_ONE_FLAG(state, dest, cur, DIRTY);
	PRINT_ONE_FLAG(state, dest, cur, UPTODATE);
	PRINT_ONE_FLAG(state, dest, cur, LOCKED);
	PRINT_ONE_FLAG(state, dest, cur, NEW);
	PRINT_ONE_FLAG(state, dest, cur, DELALLOC);
	PRINT_ONE_FLAG(state, dest, cur, DEFRAG);
	PRINT_ONE_FLAG(state, dest, cur, BOUNDARY);
	PRINT_ONE_FLAG(state, dest, cur, NODATASUM);
	PRINT_ONE_FLAG(state, dest, cur, CLEAR_META_RESV);
	PRINT_ONE_FLAG(state, dest, cur, NEED_WAIT);
	PRINT_ONE_FLAG(state, dest, cur, NORESERVE);
	PRINT_ONE_FLAG(state, dest, cur, QGROUP_RESERVED);
	PRINT_ONE_FLAG(state, dest, cur, CLEAR_DATA_RESV);
}

static void dump_extent_io_tree(const struct extent_io_tree *tree)
{
	struct rb_node *node;
	char flags_str[STATE_FLAG_STR_LEN];

	node = rb_first(&tree->state);
	test_msg("io tree content:");
	while (node) {
		struct extent_state *state;

		state = rb_entry(node, struct extent_state, rb_node);
		extent_flag_to_str(state, flags_str);
		test_msg("  start=%llu len=%llu flags=%s", state->start,
			 state->end + 1 - state->start, flags_str);
		node = rb_next(node);
	}
}

static int test_find_delalloc(u32 sectorsize)
{
	struct inode *inode;
	struct extent_io_tree *tmp;
	struct page *page;
	struct page *locked_page = NULL;
	unsigned long index = 0;
	/* In this test we need at least 2 file extents at its maximum size */
	u64 max_bytes = BTRFS_MAX_EXTENT_SIZE;
	u64 total_dirty = 2 * max_bytes;
	u64 start, end, test_start;
	bool found;
	int ret = -EINVAL;

	test_msg("running find delalloc tests");

	inode = btrfs_new_test_inode();
	if (!inode) {
		test_std_err(TEST_ALLOC_INODE);
		return -ENOMEM;
	}
	tmp = &BTRFS_I(inode)->io_tree;

	/*
	 * Passing NULL as we don't have fs_info but tracepoints are not used
	 * at this point
	 */
	extent_io_tree_init(NULL, tmp, IO_TREE_SELFTEST, NULL);

	/*
	 * First go through and create and mark all of our pages dirty, we pin
	 * everything to make sure our pages don't get evicted and screw up our
	 * test.
	 */
	for (index = 0; index < (total_dirty >> PAGE_SHIFT); index++) {
		page = find_or_create_page(inode->i_mapping, index, GFP_KERNEL);
		if (!page) {
			test_err("failed to allocate test page");
			ret = -ENOMEM;
			goto out;
		}
		SetPageDirty(page);
		if (index) {
			unlock_page(page);
		} else {
			get_page(page);
			locked_page = page;
		}
	}

	/* Test this scenario
	 * |--- delalloc ---|
	 * |---  search  ---|
	 */
	set_extent_delalloc(tmp, 0, sectorsize - 1, 0, NULL);
	start = 0;
	end = start + PAGE_SIZE - 1;
	found = find_lock_delalloc_range(inode, locked_page, &start,
					 &end);
	if (!found) {
		test_err("should have found at least one delalloc");
		goto out_bits;
	}
	if (start != 0 || end != (sectorsize - 1)) {
		test_err("expected start 0 end %u, got start %llu end %llu",
			sectorsize - 1, start, end);
		goto out_bits;
	}
	unlock_extent(tmp, start, end, NULL);
	unlock_page(locked_page);
	put_page(locked_page);

	/*
	 * Test this scenario
	 *
	 * |--- delalloc ---|
	 *           |--- search ---|
	 */
	test_start = SZ_64M;
	locked_page = find_lock_page(inode->i_mapping,
				     test_start >> PAGE_SHIFT);
	if (!locked_page) {
		test_err("couldn't find the locked page");
		goto out_bits;
	}
	set_extent_delalloc(tmp, sectorsize, max_bytes - 1, 0, NULL);
	start = test_start;
	end = start + PAGE_SIZE - 1;
	found = find_lock_delalloc_range(inode, locked_page, &start,
					 &end);
	if (!found) {
		test_err("couldn't find delalloc in our range");
		goto out_bits;
	}
	if (start != test_start || end != max_bytes - 1) {
		test_err("expected start %llu end %llu, got start %llu, end %llu",
				test_start, max_bytes - 1, start, end);
		goto out_bits;
	}
	if (process_page_range(inode, start, end,
			       PROCESS_TEST_LOCKED | PROCESS_UNLOCK)) {
		test_err("there were unlocked pages in the range");
		goto out_bits;
	}
	unlock_extent(tmp, start, end, NULL);
	/* locked_page was unlocked above */
	put_page(locked_page);

	/*
	 * Test this scenario
	 * |--- delalloc ---|
	 *                    |--- search ---|
	 */
	test_start = max_bytes + sectorsize;
	locked_page = find_lock_page(inode->i_mapping, test_start >>
				     PAGE_SHIFT);
	if (!locked_page) {
		test_err("couldn't find the locked page");
		goto out_bits;
	}
	start = test_start;
	end = start + PAGE_SIZE - 1;
	found = find_lock_delalloc_range(inode, locked_page, &start,
					 &end);
	if (found) {
		test_err("found range when we shouldn't have");
		goto out_bits;
	}
	if (end != test_start + PAGE_SIZE - 1) {
		test_err("did not return the proper end offset");
		goto out_bits;
	}

	/*
	 * Test this scenario
	 * [------- delalloc -------|
	 * [max_bytes]|-- search--|
	 *
	 * We are re-using our test_start from above since it works out well.
	 */
	set_extent_delalloc(tmp, max_bytes, total_dirty - 1, 0, NULL);
	start = test_start;
	end = start + PAGE_SIZE - 1;
	found = find_lock_delalloc_range(inode, locked_page, &start,
					 &end);
	if (!found) {
		test_err("didn't find our range");
		goto out_bits;
	}
	if (start != test_start || end != total_dirty - 1) {
		test_err("expected start %llu end %llu, got start %llu end %llu",
			 test_start, total_dirty - 1, start, end);
		goto out_bits;
	}
	if (process_page_range(inode, start, end,
			       PROCESS_TEST_LOCKED | PROCESS_UNLOCK)) {
		test_err("pages in range were not all locked");
		goto out_bits;
	}
	unlock_extent(tmp, start, end, NULL);

	/*
	 * Now to test where we run into a page that is no longer dirty in the
	 * range we want to find.
	 */
	page = find_get_page(inode->i_mapping,
			     (max_bytes + SZ_1M) >> PAGE_SHIFT);
	if (!page) {
		test_err("couldn't find our page");
		goto out_bits;
	}
	ClearPageDirty(page);
	put_page(page);

	/* We unlocked it in the previous test */
	lock_page(locked_page);
	start = test_start;
	end = start + PAGE_SIZE - 1;
	/*
	 * Currently if we fail to find dirty pages in the delalloc range we
	 * will adjust max_bytes down to PAGE_SIZE and then re-search.  If
	 * this changes at any point in the future we will need to fix this
	 * tests expected behavior.
	 */
	found = find_lock_delalloc_range(inode, locked_page, &start,
					 &end);
	if (!found) {
		test_err("didn't find our range");
		goto out_bits;
	}
	if (start != test_start && end != test_start + PAGE_SIZE - 1) {
		test_err("expected start %llu end %llu, got start %llu end %llu",
			 test_start, test_start + PAGE_SIZE - 1, start, end);
		goto out_bits;
	}
	if (process_page_range(inode, start, end, PROCESS_TEST_LOCKED |
			       PROCESS_UNLOCK)) {
		test_err("pages in range were not all locked");
		goto out_bits;
	}
	ret = 0;
out_bits:
	if (ret)
		dump_extent_io_tree(tmp);
	clear_extent_bits(tmp, 0, total_dirty - 1, (unsigned)-1);
out:
	if (locked_page)
		put_page(locked_page);
	process_page_range(inode, 0, total_dirty - 1,
			   PROCESS_UNLOCK | PROCESS_RELEASE);
	iput(inode);
	return ret;
}

static int check_eb_bitmap(unsigned long *bitmap, struct extent_buffer *eb,
			   unsigned long len)
{
	unsigned long i;

	for (i = 0; i < len * BITS_PER_BYTE; i++) {
		int bit, bit1;

		bit = !!test_bit(i, bitmap);
		bit1 = !!extent_buffer_test_bit(eb, 0, i);
		if (bit1 != bit) {
			test_err("bits do not match");
			return -EINVAL;
		}

		bit1 = !!extent_buffer_test_bit(eb, i / BITS_PER_BYTE,
						i % BITS_PER_BYTE);
		if (bit1 != bit) {
			test_err("offset bits do not match");
			return -EINVAL;
		}
	}
	return 0;
}

static int __test_eb_bitmaps(unsigned long *bitmap, struct extent_buffer *eb,
			     unsigned long len)
{
	unsigned long i, j;
	u32 x;
	int ret;

	memset(bitmap, 0, len);
	memzero_extent_buffer(eb, 0, len);
	if (memcmp_extent_buffer(eb, bitmap, 0, len) != 0) {
		test_err("bitmap was not zeroed");
		return -EINVAL;
	}

	bitmap_set(bitmap, 0, len * BITS_PER_BYTE);
	extent_buffer_bitmap_set(eb, 0, 0, len * BITS_PER_BYTE);
	ret = check_eb_bitmap(bitmap, eb, len);
	if (ret) {
		test_err("setting all bits failed");
		return ret;
	}

	bitmap_clear(bitmap, 0, len * BITS_PER_BYTE);
	extent_buffer_bitmap_clear(eb, 0, 0, len * BITS_PER_BYTE);
	ret = check_eb_bitmap(bitmap, eb, len);
	if (ret) {
		test_err("clearing all bits failed");
		return ret;
	}

	/* Straddling pages test */
	if (len > PAGE_SIZE) {
		bitmap_set(bitmap,
			(PAGE_SIZE - sizeof(long) / 2) * BITS_PER_BYTE,
			sizeof(long) * BITS_PER_BYTE);
		extent_buffer_bitmap_set(eb, PAGE_SIZE - sizeof(long) / 2, 0,
					sizeof(long) * BITS_PER_BYTE);
		ret = check_eb_bitmap(bitmap, eb, len);
		if (ret) {
			test_err("setting straddling pages failed");
			return ret;
		}

		bitmap_set(bitmap, 0, len * BITS_PER_BYTE);
		bitmap_clear(bitmap,
			(PAGE_SIZE - sizeof(long) / 2) * BITS_PER_BYTE,
			sizeof(long) * BITS_PER_BYTE);
		extent_buffer_bitmap_set(eb, 0, 0, len * BITS_PER_BYTE);
		extent_buffer_bitmap_clear(eb, PAGE_SIZE - sizeof(long) / 2, 0,
					sizeof(long) * BITS_PER_BYTE);
		ret = check_eb_bitmap(bitmap, eb, len);
		if (ret) {
			test_err("clearing straddling pages failed");
			return ret;
		}
	}

	/*
	 * Generate a wonky pseudo-random bit pattern for the sake of not using
	 * something repetitive that could miss some hypothetical off-by-n bug.
	 */
	x = 0;
	bitmap_clear(bitmap, 0, len * BITS_PER_BYTE);
	extent_buffer_bitmap_clear(eb, 0, 0, len * BITS_PER_BYTE);
	for (i = 0; i < len * BITS_PER_BYTE / 32; i++) {
		x = (0x19660dULL * (u64)x + 0x3c6ef35fULL) & 0xffffffffU;
		for (j = 0; j < 32; j++) {
			if (x & (1U << j)) {
				bitmap_set(bitmap, i * 32 + j, 1);
				extent_buffer_bitmap_set(eb, 0, i * 32 + j, 1);
			}
		}
	}

	ret = check_eb_bitmap(bitmap, eb, len);
	if (ret) {
		test_err("random bit pattern failed");
		return ret;
	}

	return 0;
}

static int test_eb_bitmaps(u32 sectorsize, u32 nodesize)
{
	struct btrfs_fs_info *fs_info;
	unsigned long *bitmap = NULL;
	struct extent_buffer *eb = NULL;
	int ret;

	test_msg("running extent buffer bitmap tests");

	fs_info = btrfs_alloc_dummy_fs_info(nodesize, sectorsize);
	if (!fs_info) {
		test_std_err(TEST_ALLOC_FS_INFO);
		return -ENOMEM;
	}

	bitmap = kmalloc(nodesize, GFP_KERNEL);
	if (!bitmap) {
		test_err("couldn't allocate test bitmap");
		ret = -ENOMEM;
		goto out;
	}

	eb = __alloc_dummy_extent_buffer(fs_info, 0, nodesize);
	if (!eb) {
		test_std_err(TEST_ALLOC_ROOT);
		ret = -ENOMEM;
		goto out;
	}

	ret = __test_eb_bitmaps(bitmap, eb, nodesize);
	if (ret)
		goto out;

	free_extent_buffer(eb);

	/*
	 * Test again for case where the tree block is sectorsize aligned but
	 * not nodesize aligned.
	 */
	eb = __alloc_dummy_extent_buffer(fs_info, sectorsize, nodesize);
	if (!eb) {
		test_std_err(TEST_ALLOC_ROOT);
		ret = -ENOMEM;
		goto out;
	}

	ret = __test_eb_bitmaps(bitmap, eb, nodesize);
out:
	free_extent_buffer(eb);
	kfree(bitmap);
	btrfs_free_dummy_fs_info(fs_info);
	return ret;
}

static int test_find_first_clear_extent_bit(void)
{
	struct extent_io_tree tree;
	u64 start, end;
	int ret = -EINVAL;

	test_msg("running find_first_clear_extent_bit test");

	extent_io_tree_init(NULL, &tree, IO_TREE_SELFTEST, NULL);

	/* Test correct handling of empty tree */
	find_first_clear_extent_bit(&tree, 0, &start, &end, CHUNK_TRIMMED);
	if (start != 0 || end != -1) {
		test_err(
	"error getting a range from completely empty tree: start %llu end %llu",
			 start, end);
		goto out;
	}
	/*
	 * Set 1M-4M alloc/discard and 32M-64M thus leaving a hole between
	 * 4M-32M
	 */
	set_extent_bits(&tree, SZ_1M, SZ_4M - 1,
			CHUNK_TRIMMED | CHUNK_ALLOCATED);

	find_first_clear_extent_bit(&tree, SZ_512K, &start, &end,
				    CHUNK_TRIMMED | CHUNK_ALLOCATED);

	if (start != 0 || end != SZ_1M - 1) {
		test_err("error finding beginning range: start %llu end %llu",
			 start, end);
		goto out;
	}

	/* Now add 32M-64M so that we have a hole between 4M-32M */
	set_extent_bits(&tree, SZ_32M, SZ_64M - 1,
			CHUNK_TRIMMED | CHUNK_ALLOCATED);

	/*
	 * Request first hole starting at 12M, we should get 4M-32M
	 */
	find_first_clear_extent_bit(&tree, 12 * SZ_1M, &start, &end,
				    CHUNK_TRIMMED | CHUNK_ALLOCATED);

	if (start != SZ_4M || end != SZ_32M - 1) {
		test_err("error finding trimmed range: start %llu end %llu",
			 start, end);
		goto out;
	}

	/*
	 * Search in the middle of allocated range, should get the next one
	 * available, which happens to be unallocated -> 4M-32M
	 */
	find_first_clear_extent_bit(&tree, SZ_2M, &start, &end,
				    CHUNK_TRIMMED | CHUNK_ALLOCATED);

	if (start != SZ_4M || end != SZ_32M - 1) {
		test_err("error finding next unalloc range: start %llu end %llu",
			 start, end);
		goto out;
	}

	/*
	 * Set 64M-72M with CHUNK_ALLOC flag, then search for CHUNK_TRIMMED flag
	 * being unset in this range, we should get the entry in range 64M-72M
	 */
	set_extent_bits(&tree, SZ_64M, SZ_64M + SZ_8M - 1, CHUNK_ALLOCATED);
	find_first_clear_extent_bit(&tree, SZ_64M + SZ_1M, &start, &end,
				    CHUNK_TRIMMED);

	if (start != SZ_64M || end != SZ_64M + SZ_8M - 1) {
		test_err("error finding exact range: start %llu end %llu",
			 start, end);
		goto out;
	}

	find_first_clear_extent_bit(&tree, SZ_64M - SZ_8M, &start, &end,
				    CHUNK_TRIMMED);

	/*
	 * Search in the middle of set range whose immediate neighbour doesn't
	 * have the bits set so it must be returned
	 */
	if (start != SZ_64M || end != SZ_64M + SZ_8M - 1) {
		test_err("error finding next alloc range: start %llu end %llu",
			 start, end);
		goto out;
	}

	/*
	 * Search beyond any known range, shall return after last known range
	 * and end should be -1
	 */
	find_first_clear_extent_bit(&tree, -1, &start, &end, CHUNK_TRIMMED);
	if (start != SZ_64M + SZ_8M || end != -1) {
		test_err(
		"error handling beyond end of range search: start %llu end %llu",
			start, end);
		goto out;
	}

	ret = 0;
out:
	if (ret)
		dump_extent_io_tree(&tree);
	clear_extent_bits(&tree, 0, (u64)-1, CHUNK_TRIMMED | CHUNK_ALLOCATED);

	return ret;
}

int btrfs_test_extent_io(u32 sectorsize, u32 nodesize)
{
	int ret;

	test_msg("running extent I/O tests");

	ret = test_find_delalloc(sectorsize);
	if (ret)
		goto out;

	ret = test_find_first_clear_extent_bit();
	if (ret)
		goto out;

	ret = test_eb_bitmaps(sectorsize, nodesize);
out:
	return ret;
}
