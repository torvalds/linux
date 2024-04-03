// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2013 Fusion IO.  All rights reserved.
 */

#include <linux/pagemap.h>
#include <linux/pagevec.h>
#include <linux/sched.h>
#include <linux/slab.h>
#include <linux/sizes.h>
#include "btrfs-tests.h"
#include "../ctree.h"
#include "../extent_io.h"
#include "../disk-io.h"
#include "../btrfs_inode.h"

#define PROCESS_UNLOCK		(1 << 0)
#define PROCESS_RELEASE		(1 << 1)
#define PROCESS_TEST_LOCKED	(1 << 2)

static noinline int process_page_range(struct inode *inode, u64 start, u64 end,
				       unsigned long flags)
{
	int ret;
	struct folio_batch fbatch;
	unsigned long index = start >> PAGE_SHIFT;
	unsigned long end_index = end >> PAGE_SHIFT;
	int i;
	int count = 0;
	int loops = 0;

	folio_batch_init(&fbatch);

	while (index <= end_index) {
		ret = filemap_get_folios_contig(inode->i_mapping, &index,
				end_index, &fbatch);
		for (i = 0; i < ret; i++) {
			struct folio *folio = fbatch.folios[i];

			if (flags & PROCESS_TEST_LOCKED &&
			    !folio_test_locked(folio))
				count++;
			if (flags & PROCESS_UNLOCK && folio_test_locked(folio))
				folio_unlock(folio);
			if (flags & PROCESS_RELEASE)
				folio_put(folio);
		}
		folio_batch_release(&fbatch);
		cond_resched();
		loops++;
		if (loops > 100000) {
			printk(KERN_ERR
		"stuck in a loop, start %llu, end %llu, ret %d\n",
				start, end, ret);
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

static int test_find_delalloc(u32 sectorsize, u32 nodesize)
{
	struct btrfs_fs_info *fs_info;
	struct btrfs_root *root = NULL;
	struct inode *inode = NULL;
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

	fs_info = btrfs_alloc_dummy_fs_info(nodesize, sectorsize);
	if (!fs_info) {
		test_std_err(TEST_ALLOC_FS_INFO);
		return -ENOMEM;
	}

	root = btrfs_alloc_dummy_root(fs_info);
	if (IS_ERR(root)) {
		test_std_err(TEST_ALLOC_ROOT);
		ret = PTR_ERR(root);
		goto out;
	}

	inode = btrfs_new_test_inode();
	if (!inode) {
		test_std_err(TEST_ALLOC_INODE);
		ret = -ENOMEM;
		goto out;
	}
	tmp = &BTRFS_I(inode)->io_tree;
	BTRFS_I(inode)->root = root;

	/*
	 * Passing NULL as we don't have fs_info but tracepoints are not used
	 * at this point
	 */
	extent_io_tree_init(NULL, tmp, IO_TREE_SELFTEST);

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
	set_extent_bit(tmp, 0, sectorsize - 1, EXTENT_DELALLOC, NULL);
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
	set_extent_bit(tmp, sectorsize, max_bytes - 1, EXTENT_DELALLOC, NULL);
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
	set_extent_bit(tmp, max_bytes, total_dirty - 1, EXTENT_DELALLOC, NULL);
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
	btrfs_free_dummy_root(root);
	btrfs_free_dummy_fs_info(fs_info);
	return ret;
}

static int check_eb_bitmap(unsigned long *bitmap, struct extent_buffer *eb)
{
	unsigned long i;

	for (i = 0; i < eb->len * BITS_PER_BYTE; i++) {
		int bit, bit1;

		bit = !!test_bit(i, bitmap);
		bit1 = !!extent_buffer_test_bit(eb, 0, i);
		if (bit1 != bit) {
			u8 has;
			u8 expect;

			read_extent_buffer(eb, &has, i / BITS_PER_BYTE, 1);
			expect = bitmap_get_value8(bitmap, ALIGN(i, BITS_PER_BYTE));

			test_err(
		"bits do not match, start byte 0 bit %lu, byte %lu has 0x%02x expect 0x%02x",
				 i, i / BITS_PER_BYTE, has, expect);
			return -EINVAL;
		}

		bit1 = !!extent_buffer_test_bit(eb, i / BITS_PER_BYTE,
						i % BITS_PER_BYTE);
		if (bit1 != bit) {
			u8 has;
			u8 expect;

			read_extent_buffer(eb, &has, i / BITS_PER_BYTE, 1);
			expect = bitmap_get_value8(bitmap, ALIGN(i, BITS_PER_BYTE));

			test_err(
		"bits do not match, start byte %lu bit %lu, byte %lu has 0x%02x expect 0x%02x",
				 i / BITS_PER_BYTE, i % BITS_PER_BYTE,
				 i / BITS_PER_BYTE, has, expect);
			return -EINVAL;
		}
	}
	return 0;
}

static int test_bitmap_set(const char *name, unsigned long *bitmap,
			   struct extent_buffer *eb,
			   unsigned long byte_start, unsigned long bit_start,
			   unsigned long bit_len)
{
	int ret;

	bitmap_set(bitmap, byte_start * BITS_PER_BYTE + bit_start, bit_len);
	extent_buffer_bitmap_set(eb, byte_start, bit_start, bit_len);
	ret = check_eb_bitmap(bitmap, eb);
	if (ret < 0)
		test_err("%s test failed", name);
	return ret;
}

static int test_bitmap_clear(const char *name, unsigned long *bitmap,
			     struct extent_buffer *eb,
			     unsigned long byte_start, unsigned long bit_start,
			     unsigned long bit_len)
{
	int ret;

	bitmap_clear(bitmap, byte_start * BITS_PER_BYTE + bit_start, bit_len);
	extent_buffer_bitmap_clear(eb, byte_start, bit_start, bit_len);
	ret = check_eb_bitmap(bitmap, eb);
	if (ret < 0)
		test_err("%s test failed", name);
	return ret;
}
static int __test_eb_bitmaps(unsigned long *bitmap, struct extent_buffer *eb)
{
	unsigned long i, j;
	unsigned long byte_len = eb->len;
	u32 x;
	int ret;

	ret = test_bitmap_clear("clear all run 1", bitmap, eb, 0, 0,
				byte_len * BITS_PER_BYTE);
	if (ret < 0)
		return ret;

	ret = test_bitmap_set("set all", bitmap, eb, 0, 0, byte_len * BITS_PER_BYTE);
	if (ret < 0)
		return ret;

	ret = test_bitmap_clear("clear all run 2", bitmap, eb, 0, 0,
				byte_len * BITS_PER_BYTE);
	if (ret < 0)
		return ret;

	ret = test_bitmap_set("same byte set", bitmap, eb, 0, 2, 4);
	if (ret < 0)
		return ret;

	ret = test_bitmap_clear("same byte partial clear", bitmap, eb, 0, 4, 1);
	if (ret < 0)
		return ret;

	ret = test_bitmap_set("cross byte set", bitmap, eb, 2, 4, 8);
	if (ret < 0)
		return ret;

	ret = test_bitmap_set("cross multi byte set", bitmap, eb, 4, 4, 24);
	if (ret < 0)
		return ret;

	ret = test_bitmap_clear("cross byte clear", bitmap, eb, 2, 6, 4);
	if (ret < 0)
		return ret;

	ret = test_bitmap_clear("cross multi byte clear", bitmap, eb, 4, 6, 20);
	if (ret < 0)
		return ret;

	/* Straddling pages test */
	if (byte_len > PAGE_SIZE) {
		ret = test_bitmap_set("cross page set", bitmap, eb,
				      PAGE_SIZE - sizeof(long) / 2, 0,
				      sizeof(long) * BITS_PER_BYTE);
		if (ret < 0)
			return ret;

		ret = test_bitmap_set("cross page set all", bitmap, eb, 0, 0,
				      byte_len * BITS_PER_BYTE);
		if (ret < 0)
			return ret;

		ret = test_bitmap_clear("cross page clear", bitmap, eb,
					PAGE_SIZE - sizeof(long) / 2, 0,
					sizeof(long) * BITS_PER_BYTE);
		if (ret < 0)
			return ret;
	}

	/*
	 * Generate a wonky pseudo-random bit pattern for the sake of not using
	 * something repetitive that could miss some hypothetical off-by-n bug.
	 */
	x = 0;
	ret = test_bitmap_clear("clear all run 3", bitmap, eb, 0, 0,
				byte_len * BITS_PER_BYTE);
	if (ret < 0)
		return ret;

	for (i = 0; i < byte_len * BITS_PER_BYTE / 32; i++) {
		x = (0x19660dULL * (u64)x + 0x3c6ef35fULL) & 0xffffffffU;
		for (j = 0; j < 32; j++) {
			if (x & (1U << j)) {
				bitmap_set(bitmap, i * 32 + j, 1);
				extent_buffer_bitmap_set(eb, 0, i * 32 + j, 1);
			}
		}
	}

	ret = check_eb_bitmap(bitmap, eb);
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

	ret = __test_eb_bitmaps(bitmap, eb);
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

	ret = __test_eb_bitmaps(bitmap, eb);
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

	extent_io_tree_init(NULL, &tree, IO_TREE_SELFTEST);

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
	set_extent_bit(&tree, SZ_1M, SZ_4M - 1,
		       CHUNK_TRIMMED | CHUNK_ALLOCATED, NULL);

	find_first_clear_extent_bit(&tree, SZ_512K, &start, &end,
				    CHUNK_TRIMMED | CHUNK_ALLOCATED);

	if (start != 0 || end != SZ_1M - 1) {
		test_err("error finding beginning range: start %llu end %llu",
			 start, end);
		goto out;
	}

	/* Now add 32M-64M so that we have a hole between 4M-32M */
	set_extent_bit(&tree, SZ_32M, SZ_64M - 1,
		       CHUNK_TRIMMED | CHUNK_ALLOCATED, NULL);

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
	set_extent_bit(&tree, SZ_64M, SZ_64M + SZ_8M - 1, CHUNK_ALLOCATED, NULL);
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

static void dump_eb_and_memory_contents(struct extent_buffer *eb, void *memory,
					const char *test_name)
{
	for (int i = 0; i < eb->len; i++) {
		struct page *page = folio_page(eb->folios[i >> PAGE_SHIFT], 0);
		void *addr = page_address(page) + offset_in_page(i);

		if (memcmp(addr, memory + i, 1) != 0) {
			test_err("%s failed", test_name);
			test_err("eb and memory diffs at byte %u, eb has 0x%02x memory has 0x%02x",
				 i, *(u8 *)addr, *(u8 *)(memory + i));
			return;
		}
	}
}

static int verify_eb_and_memory(struct extent_buffer *eb, void *memory,
				const char *test_name)
{
	for (int i = 0; i < (eb->len >> PAGE_SHIFT); i++) {
		void *eb_addr = folio_address(eb->folios[i]);

		if (memcmp(memory + (i << PAGE_SHIFT), eb_addr, PAGE_SIZE) != 0) {
			dump_eb_and_memory_contents(eb, memory, test_name);
			return -EUCLEAN;
		}
	}
	return 0;
}

/*
 * Init both memory and extent buffer contents to the same randomly generated
 * contents.
 */
static void init_eb_and_memory(struct extent_buffer *eb, void *memory)
{
	get_random_bytes(memory, eb->len);
	write_extent_buffer(eb, memory, 0, eb->len);
}

static int test_eb_mem_ops(u32 sectorsize, u32 nodesize)
{
	struct btrfs_fs_info *fs_info;
	struct extent_buffer *eb = NULL;
	void *memory = NULL;
	int ret;

	test_msg("running extent buffer memory operation tests");

	fs_info = btrfs_alloc_dummy_fs_info(nodesize, sectorsize);
	if (!fs_info) {
		test_std_err(TEST_ALLOC_FS_INFO);
		return -ENOMEM;
	}

	memory = kvzalloc(nodesize, GFP_KERNEL);
	if (!memory) {
		test_err("failed to allocate memory");
		ret = -ENOMEM;
		goto out;
	}

	eb = __alloc_dummy_extent_buffer(fs_info, SZ_1M, nodesize);
	if (!eb) {
		test_std_err(TEST_ALLOC_EXTENT_BUFFER);
		ret = -ENOMEM;
		goto out;
	}

	init_eb_and_memory(eb, memory);
	ret = verify_eb_and_memory(eb, memory, "full eb write");
	if (ret < 0)
		goto out;

	memcpy(memory, memory + 16, 16);
	memcpy_extent_buffer(eb, 0, 16, 16);
	ret = verify_eb_and_memory(eb, memory, "same page non-overlapping memcpy 1");
	if (ret < 0)
		goto out;

	memcpy(memory, memory + 2048, 16);
	memcpy_extent_buffer(eb, 0, 2048, 16);
	ret = verify_eb_and_memory(eb, memory, "same page non-overlapping memcpy 2");
	if (ret < 0)
		goto out;
	memcpy(memory, memory + 2048, 2048);
	memcpy_extent_buffer(eb, 0, 2048, 2048);
	ret = verify_eb_and_memory(eb, memory, "same page non-overlapping memcpy 3");
	if (ret < 0)
		goto out;

	memmove(memory + 512, memory + 256, 512);
	memmove_extent_buffer(eb, 512, 256, 512);
	ret = verify_eb_and_memory(eb, memory, "same page overlapping memcpy 1");
	if (ret < 0)
		goto out;

	memmove(memory + 2048, memory + 512, 2048);
	memmove_extent_buffer(eb, 2048, 512, 2048);
	ret = verify_eb_and_memory(eb, memory, "same page overlapping memcpy 2");
	if (ret < 0)
		goto out;
	memmove(memory + 512, memory + 2048, 2048);
	memmove_extent_buffer(eb, 512, 2048, 2048);
	ret = verify_eb_and_memory(eb, memory, "same page overlapping memcpy 3");
	if (ret < 0)
		goto out;

	if (nodesize > PAGE_SIZE) {
		memcpy(memory, memory + 4096 - 128, 256);
		memcpy_extent_buffer(eb, 0, 4096 - 128, 256);
		ret = verify_eb_and_memory(eb, memory, "cross page non-overlapping memcpy 1");
		if (ret < 0)
			goto out;

		memcpy(memory + 4096 - 128, memory + 4096 + 128, 256);
		memcpy_extent_buffer(eb, 4096 - 128, 4096 + 128, 256);
		ret = verify_eb_and_memory(eb, memory, "cross page non-overlapping memcpy 2");
		if (ret < 0)
			goto out;

		memmove(memory + 4096 - 128, memory + 4096 - 64, 256);
		memmove_extent_buffer(eb, 4096 - 128, 4096 - 64, 256);
		ret = verify_eb_and_memory(eb, memory, "cross page overlapping memcpy 1");
		if (ret < 0)
			goto out;

		memmove(memory + 4096 - 64, memory + 4096 - 128, 256);
		memmove_extent_buffer(eb, 4096 - 64, 4096 - 128, 256);
		ret = verify_eb_and_memory(eb, memory, "cross page overlapping memcpy 2");
		if (ret < 0)
			goto out;
	}
out:
	free_extent_buffer(eb);
	kvfree(memory);
	btrfs_free_dummy_fs_info(fs_info);
	return ret;
}

int btrfs_test_extent_io(u32 sectorsize, u32 nodesize)
{
	int ret;

	test_msg("running extent I/O tests");

	ret = test_find_delalloc(sectorsize, nodesize);
	if (ret)
		goto out;

	ret = test_find_first_clear_extent_bit();
	if (ret)
		goto out;

	ret = test_eb_bitmaps(sectorsize, nodesize);
	if (ret)
		goto out;

	ret = test_eb_mem_ops(sectorsize, nodesize);
out:
	return ret;
}
