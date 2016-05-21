/*
 * multiorder.c: Multi-order radix tree entry testing
 * Copyright (c) 2016 Intel Corporation
 * Author: Ross Zwisler <ross.zwisler@linux.intel.com>
 * Author: Matthew Wilcox <matthew.r.wilcox@intel.com>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 */
#include <linux/radix-tree.h>
#include <linux/slab.h>
#include <linux/errno.h>

#include "test.h"

#define for_each_index(i, base, order) \
	for (i = base; i < base + (1 << order); i++)

static void __multiorder_tag_test(int index, int order)
{
	RADIX_TREE(tree, GFP_KERNEL);
	int base, err, i;
	unsigned long first = 0;

	/* our canonical entry */
	base = index & ~((1 << order) - 1);

	printf("Multiorder tag test with index %d, canonical entry %d\n",
			index, base);

	err = item_insert_order(&tree, index, order);
	assert(!err);

	/*
	 * Verify we get collisions for covered indices.  We try and fail to
	 * insert an exceptional entry so we don't leak memory via
	 * item_insert_order().
	 */
	for_each_index(i, base, order) {
		err = __radix_tree_insert(&tree, i, order,
				(void *)(0xA0 | RADIX_TREE_EXCEPTIONAL_ENTRY));
		assert(err == -EEXIST);
	}

	for_each_index(i, base, order) {
		assert(!radix_tree_tag_get(&tree, i, 0));
		assert(!radix_tree_tag_get(&tree, i, 1));
	}

	assert(radix_tree_tag_set(&tree, index, 0));

	for_each_index(i, base, order) {
		assert(radix_tree_tag_get(&tree, i, 0));
		assert(!radix_tree_tag_get(&tree, i, 1));
	}

	assert(radix_tree_range_tag_if_tagged(&tree, &first, ~0UL, 10, 0, 1) == 1);
	assert(radix_tree_tag_clear(&tree, index, 0));

	for_each_index(i, base, order) {
		assert(!radix_tree_tag_get(&tree, i, 0));
		assert(radix_tree_tag_get(&tree, i, 1));
	}

	assert(radix_tree_tag_clear(&tree, index, 1));

	assert(!radix_tree_tagged(&tree, 0));
	assert(!radix_tree_tagged(&tree, 1));

	item_kill_tree(&tree);
}

static void multiorder_tag_tests(void)
{
	/* test multi-order entry for indices 0-7 with no sibling pointers */
	__multiorder_tag_test(0, 3);
	__multiorder_tag_test(5, 3);

	/* test multi-order entry for indices 8-15 with no sibling pointers */
	__multiorder_tag_test(8, 3);
	__multiorder_tag_test(15, 3);

	/*
	 * Our order 5 entry covers indices 0-31 in a tree with height=2.
	 * This is broken up as follows:
	 * 0-7:		canonical entry
	 * 8-15:	sibling 1
	 * 16-23:	sibling 2
	 * 24-31:	sibling 3
	 */
	__multiorder_tag_test(0, 5);
	__multiorder_tag_test(29, 5);

	/* same test, but with indices 32-63 */
	__multiorder_tag_test(32, 5);
	__multiorder_tag_test(44, 5);

	/*
	 * Our order 8 entry covers indices 0-255 in a tree with height=3.
	 * This is broken up as follows:
	 * 0-63:	canonical entry
	 * 64-127:	sibling 1
	 * 128-191:	sibling 2
	 * 192-255:	sibling 3
	 */
	__multiorder_tag_test(0, 8);
	__multiorder_tag_test(190, 8);

	/* same test, but with indices 256-511 */
	__multiorder_tag_test(256, 8);
	__multiorder_tag_test(300, 8);

	__multiorder_tag_test(0x12345678UL, 8);
}

static void multiorder_check(unsigned long index, int order)
{
	unsigned long i;
	unsigned long min = index & ~((1UL << order) - 1);
	unsigned long max = min + (1UL << order);
	RADIX_TREE(tree, GFP_KERNEL);

	printf("Multiorder index %ld, order %d\n", index, order);

	assert(item_insert_order(&tree, index, order) == 0);

	for (i = min; i < max; i++) {
		struct item *item = item_lookup(&tree, i);
		assert(item != 0);
		assert(item->index == index);
	}
	for (i = 0; i < min; i++)
		item_check_absent(&tree, i);
	for (i = max; i < 2*max; i++)
		item_check_absent(&tree, i);
	for (i = min; i < max; i++) {
		static void *entry = (void *)
					(0xA0 | RADIX_TREE_EXCEPTIONAL_ENTRY);
		assert(radix_tree_insert(&tree, i, entry) == -EEXIST);
	}

	assert(item_delete(&tree, index) != 0);

	for (i = 0; i < 2*max; i++)
		item_check_absent(&tree, i);
}

static void multiorder_shrink(unsigned long index, int order)
{
	unsigned long i;
	unsigned long max = 1 << order;
	RADIX_TREE(tree, GFP_KERNEL);
	struct radix_tree_node *node;

	printf("Multiorder shrink index %ld, order %d\n", index, order);

	assert(item_insert_order(&tree, 0, order) == 0);

	node = tree.rnode;

	assert(item_insert(&tree, index) == 0);
	assert(node != tree.rnode);

	assert(item_delete(&tree, index) != 0);
	assert(node == tree.rnode);

	for (i = 0; i < max; i++) {
		struct item *item = item_lookup(&tree, i);
		assert(item != 0);
		assert(item->index == 0);
	}
	for (i = max; i < 2*max; i++)
		item_check_absent(&tree, i);

	if (!item_delete(&tree, 0)) {
		printf("failed to delete index %ld (order %d)\n", index, order);		abort();
	}

	for (i = 0; i < 2*max; i++)
		item_check_absent(&tree, i);
}

static void multiorder_insert_bug(void)
{
	RADIX_TREE(tree, GFP_KERNEL);

	item_insert(&tree, 0);
	radix_tree_tag_set(&tree, 0, 0);
	item_insert_order(&tree, 3 << 6, 6);

	item_kill_tree(&tree);
}

void multiorder_iteration(void)
{
	RADIX_TREE(tree, GFP_KERNEL);
	struct radix_tree_iter iter;
	void **slot;
	int i, err;

	printf("Multiorder iteration test\n");

#define NUM_ENTRIES 11
	int index[NUM_ENTRIES] = {0, 2, 4, 8, 16, 32, 34, 36, 64, 72, 128};
	int order[NUM_ENTRIES] = {1, 1, 2, 3,  4,  1,  0,  1,  3,  0, 7};

	for (i = 0; i < NUM_ENTRIES; i++) {
		err = item_insert_order(&tree, index[i], order[i]);
		assert(!err);
	}

	i = 0;
	/* start from index 1 to verify we find the multi-order entry at 0 */
	radix_tree_for_each_slot(slot, &tree, &iter, 1) {
		int height = order[i] / RADIX_TREE_MAP_SHIFT;
		int shift = height * RADIX_TREE_MAP_SHIFT;

		assert(iter.index == index[i]);
		assert(iter.shift == shift);
		i++;
	}

	/*
	 * Now iterate through the tree starting at an elevated multi-order
	 * entry, beginning at an index in the middle of the range.
	 */
	i = 8;
	radix_tree_for_each_slot(slot, &tree, &iter, 70) {
		int height = order[i] / RADIX_TREE_MAP_SHIFT;
		int shift = height * RADIX_TREE_MAP_SHIFT;

		assert(iter.index == index[i]);
		assert(iter.shift == shift);
		i++;
	}

	item_kill_tree(&tree);
}

void multiorder_tagged_iteration(void)
{
	RADIX_TREE(tree, GFP_KERNEL);
	struct radix_tree_iter iter;
	void **slot;
	unsigned long first = 0;
	int i;

	printf("Multiorder tagged iteration test\n");

#define MT_NUM_ENTRIES 9
	int index[MT_NUM_ENTRIES] = {0, 2, 4, 16, 32, 40, 64, 72, 128};
	int order[MT_NUM_ENTRIES] = {1, 0, 2, 4,  3,  1,  3,  0,   7};

#define TAG_ENTRIES 7
	int tag_index[TAG_ENTRIES] = {0, 4, 16, 40, 64, 72, 128};

	for (i = 0; i < MT_NUM_ENTRIES; i++)
		assert(!item_insert_order(&tree, index[i], order[i]));

	assert(!radix_tree_tagged(&tree, 1));

	for (i = 0; i < TAG_ENTRIES; i++)
		assert(radix_tree_tag_set(&tree, tag_index[i], 1));

	i = 0;
	/* start from index 1 to verify we find the multi-order entry at 0 */
	radix_tree_for_each_tagged(slot, &tree, &iter, 1, 1) {
		assert(iter.index == tag_index[i]);
		i++;
	}

	/*
	 * Now iterate through the tree starting at an elevated multi-order
	 * entry, beginning at an index in the middle of the range.
	 */
	i = 4;
	radix_tree_for_each_slot(slot, &tree, &iter, 70) {
		assert(iter.index == tag_index[i]);
		i++;
	}

	radix_tree_range_tag_if_tagged(&tree, &first, ~0UL,
					MT_NUM_ENTRIES, 1, 2);

	i = 0;
	radix_tree_for_each_tagged(slot, &tree, &iter, 1, 2) {
		assert(iter.index == tag_index[i]);
		i++;
	}

	first = 1;
	radix_tree_range_tag_if_tagged(&tree, &first, ~0UL,
					MT_NUM_ENTRIES, 1, 0);
	i = 0;
	radix_tree_for_each_tagged(slot, &tree, &iter, 0, 0) {
		assert(iter.index == tag_index[i]);
		i++;
	}

	item_kill_tree(&tree);
}

void multiorder_checks(void)
{
	int i;

	for (i = 0; i < 20; i++) {
		multiorder_check(200, i);
		multiorder_check(0, i);
		multiorder_check((1UL << i) + 1, i);
	}

	for (i = 0; i < 15; i++)
		multiorder_shrink((1UL << (i + RADIX_TREE_MAP_SHIFT)), i);

	multiorder_insert_bug();
	multiorder_tag_tests();
	multiorder_iteration();
	multiorder_tagged_iteration();
}
