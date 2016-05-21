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
}
