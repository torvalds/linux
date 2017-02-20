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

	assert(tag_tagged_items(&tree, NULL, 0, ~0UL, 10, 0, 1) == 1);
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

static void __multiorder_tag_test2(unsigned order, unsigned long index2)
{
	RADIX_TREE(tree, GFP_KERNEL);
	unsigned long index = (1 << order);
	index2 += index;

	assert(item_insert_order(&tree, 0, order) == 0);
	assert(item_insert(&tree, index2) == 0);

	assert(radix_tree_tag_set(&tree, 0, 0));
	assert(radix_tree_tag_set(&tree, index2, 0));

	assert(tag_tagged_items(&tree, NULL, 0, ~0UL, 10, 0, 1) == 2);

	item_kill_tree(&tree);
}

static void multiorder_tag_tests(void)
{
	int i, j;

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

	for (i = 1; i < 10; i++)
		for (j = 0; j < (10 << i); j++)
			__multiorder_tag_test2(i, j);
}

static void multiorder_check(unsigned long index, int order)
{
	unsigned long i;
	unsigned long min = index & ~((1UL << order) - 1);
	unsigned long max = min + (1UL << order);
	void **slot;
	struct item *item2 = item_create(min, order);
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
	for (i = min; i < max; i++)
		assert(radix_tree_insert(&tree, i, item2) == -EEXIST);

	slot = radix_tree_lookup_slot(&tree, index);
	free(*slot);
	radix_tree_replace_slot(&tree, slot, item2);
	for (i = min; i < max; i++) {
		struct item *item = item_lookup(&tree, i);
		assert(item != 0);
		assert(item->index == min);
	}

	assert(item_delete(&tree, min) != 0);

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
	int i, j, err;

	printf("Multiorder iteration test\n");

#define NUM_ENTRIES 11
	int index[NUM_ENTRIES] = {0, 2, 4, 8, 16, 32, 34, 36, 64, 72, 128};
	int order[NUM_ENTRIES] = {1, 1, 2, 3,  4,  1,  0,  1,  3,  0, 7};

	for (i = 0; i < NUM_ENTRIES; i++) {
		err = item_insert_order(&tree, index[i], order[i]);
		assert(!err);
	}

	for (j = 0; j < 256; j++) {
		for (i = 0; i < NUM_ENTRIES; i++)
			if (j <= (index[i] | ((1 << order[i]) - 1)))
				break;

		radix_tree_for_each_slot(slot, &tree, &iter, j) {
			int height = order[i] / RADIX_TREE_MAP_SHIFT;
			int shift = height * RADIX_TREE_MAP_SHIFT;
			unsigned long mask = (1UL << order[i]) - 1;
			struct item *item = *slot;

			assert((iter.index | mask) == (index[i] | mask));
			assert(iter.shift == shift);
			assert(!radix_tree_is_internal_node(item));
			assert((item->index | mask) == (index[i] | mask));
			assert(item->order == order[i]);
			i++;
		}
	}

	item_kill_tree(&tree);
}

void multiorder_tagged_iteration(void)
{
	RADIX_TREE(tree, GFP_KERNEL);
	struct radix_tree_iter iter;
	void **slot;
	int i, j;

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

	for (j = 0; j < 256; j++) {
		int k;

		for (i = 0; i < TAG_ENTRIES; i++) {
			for (k = i; index[k] < tag_index[i]; k++)
				;
			if (j <= (index[k] | ((1 << order[k]) - 1)))
				break;
		}

		radix_tree_for_each_tagged(slot, &tree, &iter, j, 1) {
			unsigned long mask;
			struct item *item = *slot;
			for (k = i; index[k] < tag_index[i]; k++)
				;
			mask = (1UL << order[k]) - 1;

			assert((iter.index | mask) == (tag_index[i] | mask));
			assert(!radix_tree_is_internal_node(item));
			assert((item->index | mask) == (tag_index[i] | mask));
			assert(item->order == order[k]);
			i++;
		}
	}

	assert(tag_tagged_items(&tree, NULL, 0, ~0UL, TAG_ENTRIES, 1, 2) ==
				TAG_ENTRIES);

	for (j = 0; j < 256; j++) {
		int mask, k;

		for (i = 0; i < TAG_ENTRIES; i++) {
			for (k = i; index[k] < tag_index[i]; k++)
				;
			if (j <= (index[k] | ((1 << order[k]) - 1)))
				break;
		}

		radix_tree_for_each_tagged(slot, &tree, &iter, j, 2) {
			struct item *item = *slot;
			for (k = i; index[k] < tag_index[i]; k++)
				;
			mask = (1 << order[k]) - 1;

			assert((iter.index | mask) == (tag_index[i] | mask));
			assert(!radix_tree_is_internal_node(item));
			assert((item->index | mask) == (tag_index[i] | mask));
			assert(item->order == order[k]);
			i++;
		}
	}

	assert(tag_tagged_items(&tree, NULL, 1, ~0UL, MT_NUM_ENTRIES * 2, 1, 0)
			== TAG_ENTRIES);
	i = 0;
	radix_tree_for_each_tagged(slot, &tree, &iter, 0, 0) {
		assert(iter.index == tag_index[i]);
		i++;
	}

	item_kill_tree(&tree);
}

static void multiorder_join1(unsigned long index,
				unsigned order1, unsigned order2)
{
	unsigned long loc;
	void *item, *item2 = item_create(index + 1, order1);
	RADIX_TREE(tree, GFP_KERNEL);

	item_insert_order(&tree, index, order2);
	item = radix_tree_lookup(&tree, index);
	radix_tree_join(&tree, index + 1, order1, item2);
	loc = find_item(&tree, item);
	if (loc == -1)
		free(item);
	item = radix_tree_lookup(&tree, index + 1);
	assert(item == item2);
	item_kill_tree(&tree);
}

static void multiorder_join2(unsigned order1, unsigned order2)
{
	RADIX_TREE(tree, GFP_KERNEL);
	struct radix_tree_node *node;
	void *item1 = item_create(0, order1);
	void *item2;

	item_insert_order(&tree, 0, order2);
	radix_tree_insert(&tree, 1 << order2, (void *)0x12UL);
	item2 = __radix_tree_lookup(&tree, 1 << order2, &node, NULL);
	assert(item2 == (void *)0x12UL);
	assert(node->exceptional == 1);

	radix_tree_join(&tree, 0, order1, item1);
	item2 = __radix_tree_lookup(&tree, 1 << order2, &node, NULL);
	assert(item2 == item1);
	assert(node->exceptional == 0);
	item_kill_tree(&tree);
}

/*
 * This test revealed an accounting bug for exceptional entries at one point.
 * Nodes were being freed back into the pool with an elevated exception count
 * by radix_tree_join() and then radix_tree_split() was failing to zero the
 * count of exceptional entries.
 */
static void multiorder_join3(unsigned int order)
{
	RADIX_TREE(tree, GFP_KERNEL);
	struct radix_tree_node *node;
	void **slot;
	struct radix_tree_iter iter;
	unsigned long i;

	for (i = 0; i < (1 << order); i++) {
		radix_tree_insert(&tree, i, (void *)0x12UL);
	}

	radix_tree_join(&tree, 0, order, (void *)0x16UL);
	rcu_barrier();

	radix_tree_split(&tree, 0, 0);

	radix_tree_for_each_slot(slot, &tree, &iter, 0) {
		radix_tree_iter_replace(&tree, &iter, slot, (void *)0x12UL);
	}

	__radix_tree_lookup(&tree, 0, &node, NULL);
	assert(node->exceptional == node->count);

	item_kill_tree(&tree);
}

static void multiorder_join(void)
{
	int i, j, idx;

	for (idx = 0; idx < 1024; idx = idx * 2 + 3) {
		for (i = 1; i < 15; i++) {
			for (j = 0; j < i; j++) {
				multiorder_join1(idx, i, j);
			}
		}
	}

	for (i = 1; i < 15; i++) {
		for (j = 0; j < i; j++) {
			multiorder_join2(i, j);
		}
	}

	for (i = 3; i < 10; i++) {
		multiorder_join3(i);
	}
}

static void check_mem(unsigned old_order, unsigned new_order, unsigned alloc)
{
	struct radix_tree_preload *rtp = &radix_tree_preloads;
	if (rtp->nr != 0)
		printf("split(%u %u) remaining %u\n", old_order, new_order,
							rtp->nr);
	/*
	 * Can't check for equality here as some nodes may have been
	 * RCU-freed while we ran.  But we should never finish with more
	 * nodes allocated since they should have all been preloaded.
	 */
	if (nr_allocated > alloc)
		printf("split(%u %u) allocated %u %u\n", old_order, new_order,
							alloc, nr_allocated);
}

static void __multiorder_split(int old_order, int new_order)
{
	RADIX_TREE(tree, GFP_ATOMIC);
	void **slot;
	struct radix_tree_iter iter;
	unsigned alloc;

	radix_tree_preload(GFP_KERNEL);
	assert(item_insert_order(&tree, 0, old_order) == 0);
	radix_tree_preload_end();

	/* Wipe out the preloaded cache or it'll confuse check_mem() */
	radix_tree_cpu_dead(0);

	radix_tree_tag_set(&tree, 0, 2);

	radix_tree_split_preload(old_order, new_order, GFP_KERNEL);
	alloc = nr_allocated;
	radix_tree_split(&tree, 0, new_order);
	check_mem(old_order, new_order, alloc);
	radix_tree_for_each_slot(slot, &tree, &iter, 0) {
		radix_tree_iter_replace(&tree, &iter, slot,
					item_create(iter.index, new_order));
	}
	radix_tree_preload_end();

	item_kill_tree(&tree);
}

static void __multiorder_split2(int old_order, int new_order)
{
	RADIX_TREE(tree, GFP_KERNEL);
	void **slot;
	struct radix_tree_iter iter;
	struct radix_tree_node *node;
	void *item;

	__radix_tree_insert(&tree, 0, old_order, (void *)0x12);

	item = __radix_tree_lookup(&tree, 0, &node, NULL);
	assert(item == (void *)0x12);
	assert(node->exceptional > 0);

	radix_tree_split(&tree, 0, new_order);
	radix_tree_for_each_slot(slot, &tree, &iter, 0) {
		radix_tree_iter_replace(&tree, &iter, slot,
					item_create(iter.index, new_order));
	}

	item = __radix_tree_lookup(&tree, 0, &node, NULL);
	assert(item != (void *)0x12);
	assert(node->exceptional == 0);

	item_kill_tree(&tree);
}

static void __multiorder_split3(int old_order, int new_order)
{
	RADIX_TREE(tree, GFP_KERNEL);
	void **slot;
	struct radix_tree_iter iter;
	struct radix_tree_node *node;
	void *item;

	__radix_tree_insert(&tree, 0, old_order, (void *)0x12);

	item = __radix_tree_lookup(&tree, 0, &node, NULL);
	assert(item == (void *)0x12);
	assert(node->exceptional > 0);

	radix_tree_split(&tree, 0, new_order);
	radix_tree_for_each_slot(slot, &tree, &iter, 0) {
		radix_tree_iter_replace(&tree, &iter, slot, (void *)0x16);
	}

	item = __radix_tree_lookup(&tree, 0, &node, NULL);
	assert(item == (void *)0x16);
	assert(node->exceptional > 0);

	item_kill_tree(&tree);

	__radix_tree_insert(&tree, 0, old_order, (void *)0x12);

	item = __radix_tree_lookup(&tree, 0, &node, NULL);
	assert(item == (void *)0x12);
	assert(node->exceptional > 0);

	radix_tree_split(&tree, 0, new_order);
	radix_tree_for_each_slot(slot, &tree, &iter, 0) {
		if (iter.index == (1 << new_order))
			radix_tree_iter_replace(&tree, &iter, slot,
						(void *)0x16);
		else
			radix_tree_iter_replace(&tree, &iter, slot, NULL);
	}

	item = __radix_tree_lookup(&tree, 1 << new_order, &node, NULL);
	assert(item == (void *)0x16);
	assert(node->count == node->exceptional);
	do {
		node = node->parent;
		if (!node)
			break;
		assert(node->count == 1);
		assert(node->exceptional == 0);
	} while (1);

	item_kill_tree(&tree);
}

static void multiorder_split(void)
{
	int i, j;

	for (i = 3; i < 11; i++)
		for (j = 0; j < i; j++) {
			__multiorder_split(i, j);
			__multiorder_split2(i, j);
			__multiorder_split3(i, j);
		}
}

static void multiorder_account(void)
{
	RADIX_TREE(tree, GFP_KERNEL);
	struct radix_tree_node *node;
	void **slot;

	item_insert_order(&tree, 0, 5);

	__radix_tree_insert(&tree, 1 << 5, 5, (void *)0x12);
	__radix_tree_lookup(&tree, 0, &node, NULL);
	assert(node->count == node->exceptional * 2);
	radix_tree_delete(&tree, 1 << 5);
	assert(node->exceptional == 0);

	__radix_tree_insert(&tree, 1 << 5, 5, (void *)0x12);
	__radix_tree_lookup(&tree, 1 << 5, &node, &slot);
	assert(node->count == node->exceptional * 2);
	__radix_tree_replace(&tree, node, slot, NULL, NULL, NULL);
	assert(node->exceptional == 0);

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
	multiorder_join();
	multiorder_split();
	multiorder_account();

	radix_tree_cpu_dead(0);
}
