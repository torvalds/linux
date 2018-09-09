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
#include <pthread.h>

#include "test.h"

void multiorder_iteration(void)
{
	RADIX_TREE(tree, GFP_KERNEL);
	struct radix_tree_iter iter;
	void **slot;
	int i, j, err;

	printv(1, "Multiorder iteration test\n");

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

	printv(1, "Multiorder tagged iteration test\n");

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

	assert(tag_tagged_items(&tree, 0, ~0UL, TAG_ENTRIES, XA_MARK_1,
				XA_MARK_2) == TAG_ENTRIES);

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

	assert(tag_tagged_items(&tree, 1, ~0UL, MT_NUM_ENTRIES * 2, XA_MARK_1,
				XA_MARK_0) == TAG_ENTRIES);
	i = 0;
	radix_tree_for_each_tagged(slot, &tree, &iter, 0, 0) {
		assert(iter.index == tag_index[i]);
		i++;
	}

	item_kill_tree(&tree);
}

bool stop_iteration = false;

static void *creator_func(void *ptr)
{
	/* 'order' is set up to ensure we have sibling entries */
	unsigned int order = RADIX_TREE_MAP_SHIFT - 1;
	struct radix_tree_root *tree = ptr;
	int i;

	for (i = 0; i < 10000; i++) {
		item_insert_order(tree, 0, order);
		item_delete_rcu(tree, 0);
	}

	stop_iteration = true;
	return NULL;
}

static void *iterator_func(void *ptr)
{
	struct radix_tree_root *tree = ptr;
	struct radix_tree_iter iter;
	struct item *item;
	void **slot;

	while (!stop_iteration) {
		rcu_read_lock();
		radix_tree_for_each_slot(slot, tree, &iter, 0) {
			item = radix_tree_deref_slot(slot);

			if (!item)
				continue;
			if (radix_tree_deref_retry(item)) {
				slot = radix_tree_iter_retry(&iter);
				continue;
			}

			item_sanity(item, iter.index);
		}
		rcu_read_unlock();
	}
	return NULL;
}

static void multiorder_iteration_race(void)
{
	const int num_threads = sysconf(_SC_NPROCESSORS_ONLN);
	pthread_t worker_thread[num_threads];
	RADIX_TREE(tree, GFP_KERNEL);
	int i;

	pthread_create(&worker_thread[0], NULL, &creator_func, &tree);
	for (i = 1; i < num_threads; i++)
		pthread_create(&worker_thread[i], NULL, &iterator_func, &tree);

	for (i = 0; i < num_threads; i++)
		pthread_join(worker_thread[i], NULL);

	item_kill_tree(&tree);
}

void multiorder_checks(void)
{
	multiorder_iteration();
	multiorder_tagged_iteration();
	multiorder_iteration_race();

	radix_tree_cpu_dead(0);
}

int __weak main(void)
{
	radix_tree_init();
	multiorder_checks();
	return 0;
}
