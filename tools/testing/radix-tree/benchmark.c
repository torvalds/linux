/*
 * benchmark.c:
 * Author: Konstantin Khlebnikov <koct9i@gmail.com>
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
#include <time.h>
#include "test.h"

#define for_each_index(i, base, order) \
	        for (i = base; i < base + (1 << order); i++)

#define NSEC_PER_SEC	1000000000L

static long long benchmark_iter(struct radix_tree_root *root, bool tagged)
{
	volatile unsigned long sink = 0;
	struct radix_tree_iter iter;
	struct timespec start, finish;
	long long nsec;
	int l, loops = 1;
	void **slot;

#ifdef BENCHMARK
again:
#endif
	clock_gettime(CLOCK_MONOTONIC, &start);
	for (l = 0; l < loops; l++) {
		if (tagged) {
			radix_tree_for_each_tagged(slot, root, &iter, 0, 0)
				sink ^= (unsigned long)slot;
		} else {
			radix_tree_for_each_slot(slot, root, &iter, 0)
				sink ^= (unsigned long)slot;
		}
	}
	clock_gettime(CLOCK_MONOTONIC, &finish);

	nsec = (finish.tv_sec - start.tv_sec) * NSEC_PER_SEC +
	       (finish.tv_nsec - start.tv_nsec);

#ifdef BENCHMARK
	if (loops == 1 && nsec * 5 < NSEC_PER_SEC) {
		loops = NSEC_PER_SEC / nsec / 4 + 1;
		goto again;
	}
#endif

	nsec /= loops;
	return nsec;
}

static void benchmark_insert(struct radix_tree_root *root,
			     unsigned long size, unsigned long step, int order)
{
	struct timespec start, finish;
	unsigned long index;
	long long nsec;

	clock_gettime(CLOCK_MONOTONIC, &start);

	for (index = 0 ; index < size ; index += step)
		item_insert_order(root, index, order);

	clock_gettime(CLOCK_MONOTONIC, &finish);

	nsec = (finish.tv_sec - start.tv_sec) * NSEC_PER_SEC +
	       (finish.tv_nsec - start.tv_nsec);

	printv(2, "Size: %8ld, step: %8ld, order: %d, insertion: %15lld ns\n",
		size, step, order, nsec);
}

static void benchmark_tagging(struct radix_tree_root *root,
			     unsigned long size, unsigned long step, int order)
{
	struct timespec start, finish;
	unsigned long index;
	long long nsec;

	clock_gettime(CLOCK_MONOTONIC, &start);

	for (index = 0 ; index < size ; index += step)
		radix_tree_tag_set(root, index, 0);

	clock_gettime(CLOCK_MONOTONIC, &finish);

	nsec = (finish.tv_sec - start.tv_sec) * NSEC_PER_SEC +
	       (finish.tv_nsec - start.tv_nsec);

	printv(2, "Size: %8ld, step: %8ld, order: %d, tagging: %17lld ns\n",
		size, step, order, nsec);
}

static void benchmark_delete(struct radix_tree_root *root,
			     unsigned long size, unsigned long step, int order)
{
	struct timespec start, finish;
	unsigned long index, i;
	long long nsec;

	clock_gettime(CLOCK_MONOTONIC, &start);

	for (index = 0 ; index < size ; index += step)
		for_each_index(i, index, order)
			item_delete(root, i);

	clock_gettime(CLOCK_MONOTONIC, &finish);

	nsec = (finish.tv_sec - start.tv_sec) * NSEC_PER_SEC +
	       (finish.tv_nsec - start.tv_nsec);

	printv(2, "Size: %8ld, step: %8ld, order: %d, deletion: %16lld ns\n",
		size, step, order, nsec);
}

static void benchmark_size(unsigned long size, unsigned long step, int order)
{
	RADIX_TREE(tree, GFP_KERNEL);
	long long normal, tagged;

	benchmark_insert(&tree, size, step, order);
	benchmark_tagging(&tree, size, step, order);

	tagged = benchmark_iter(&tree, true);
	normal = benchmark_iter(&tree, false);

	printv(2, "Size: %8ld, step: %8ld, order: %d, tagged iteration: %8lld ns\n",
		size, step, order, tagged);
	printv(2, "Size: %8ld, step: %8ld, order: %d, normal iteration: %8lld ns\n",
		size, step, order, normal);

	benchmark_delete(&tree, size, step, order);

	item_kill_tree(&tree);
	rcu_barrier();
}

static long long  __benchmark_split(unsigned long index,
				    int old_order, int new_order)
{
	struct timespec start, finish;
	long long nsec;
	RADIX_TREE(tree, GFP_ATOMIC);

	item_insert_order(&tree, index, old_order);

	clock_gettime(CLOCK_MONOTONIC, &start);
	radix_tree_split(&tree, index, new_order);
	clock_gettime(CLOCK_MONOTONIC, &finish);
	nsec = (finish.tv_sec - start.tv_sec) * NSEC_PER_SEC +
	       (finish.tv_nsec - start.tv_nsec);

	item_kill_tree(&tree);

	return nsec;

}

static void benchmark_split(unsigned long size, unsigned long step)
{
	int i, j, idx;
	long long nsec = 0;


	for (idx = 0; idx < size; idx += step) {
		for (i = 3; i < 11; i++) {
			for (j = 0; j < i; j++) {
				nsec += __benchmark_split(idx, i, j);
			}
		}
	}

	printv(2, "Size %8ld, step %8ld, split time %10lld ns\n",
			size, step, nsec);

}

static long long  __benchmark_join(unsigned long index,
			     unsigned order1, unsigned order2)
{
	unsigned long loc;
	struct timespec start, finish;
	long long nsec;
	void *item, *item2 = item_create(index + 1, order1);
	RADIX_TREE(tree, GFP_KERNEL);

	item_insert_order(&tree, index, order2);
	item = radix_tree_lookup(&tree, index);

	clock_gettime(CLOCK_MONOTONIC, &start);
	radix_tree_join(&tree, index + 1, order1, item2);
	clock_gettime(CLOCK_MONOTONIC, &finish);
	nsec = (finish.tv_sec - start.tv_sec) * NSEC_PER_SEC +
		(finish.tv_nsec - start.tv_nsec);

	loc = find_item(&tree, item);
	if (loc == -1)
		free(item);

	item_kill_tree(&tree);

	return nsec;
}

static void benchmark_join(unsigned long step)
{
	int i, j, idx;
	long long nsec = 0;

	for (idx = 0; idx < 1 << 10; idx += step) {
		for (i = 1; i < 15; i++) {
			for (j = 0; j < i; j++) {
				nsec += __benchmark_join(idx, i, j);
			}
		}
	}

	printv(2, "Size %8d, step %8ld, join time %10lld ns\n",
			1 << 10, step, nsec);
}

void benchmark(void)
{
	unsigned long size[] = {1 << 10, 1 << 20, 0};
	unsigned long step[] = {1, 2, 7, 15, 63, 64, 65,
				128, 256, 512, 12345, 0};
	int c, s;

	printv(1, "starting benchmarks\n");
	printv(1, "RADIX_TREE_MAP_SHIFT = %d\n", RADIX_TREE_MAP_SHIFT);

	for (c = 0; size[c]; c++)
		for (s = 0; step[s]; s++)
			benchmark_size(size[c], step[s], 0);

	for (c = 0; size[c]; c++)
		for (s = 0; step[s]; s++)
			benchmark_size(size[c], step[s] << 9, 9);

	for (c = 0; size[c]; c++)
		for (s = 0; step[s]; s++)
			benchmark_split(size[c], step[s]);

	for (s = 0; step[s]; s++)
		benchmark_join(step[s]);
}
