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
			     unsigned long size, unsigned long step)
{
	struct timespec start, finish;
	unsigned long index;
	long long nsec;

	clock_gettime(CLOCK_MONOTONIC, &start);

	for (index = 0 ; index < size ; index += step)
		item_insert(root, index);

	clock_gettime(CLOCK_MONOTONIC, &finish);

	nsec = (finish.tv_sec - start.tv_sec) * NSEC_PER_SEC +
	       (finish.tv_nsec - start.tv_nsec);

	printv(2, "Size: %8ld, step: %8ld, insertion: %15lld ns\n",
		size, step, nsec);
}

static void benchmark_tagging(struct radix_tree_root *root,
			     unsigned long size, unsigned long step)
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

	printv(2, "Size: %8ld, step: %8ld, tagging: %17lld ns\n",
		size, step, nsec);
}

static void benchmark_delete(struct radix_tree_root *root,
			     unsigned long size, unsigned long step)
{
	struct timespec start, finish;
	unsigned long index;
	long long nsec;

	clock_gettime(CLOCK_MONOTONIC, &start);

	for (index = 0 ; index < size ; index += step)
		item_delete(root, index);

	clock_gettime(CLOCK_MONOTONIC, &finish);

	nsec = (finish.tv_sec - start.tv_sec) * NSEC_PER_SEC +
	       (finish.tv_nsec - start.tv_nsec);

	printv(2, "Size: %8ld, step: %8ld, deletion: %16lld ns\n",
		size, step, nsec);
}

static void benchmark_size(unsigned long size, unsigned long step)
{
	RADIX_TREE(tree, GFP_KERNEL);
	long long normal, tagged;

	benchmark_insert(&tree, size, step);
	benchmark_tagging(&tree, size, step);

	tagged = benchmark_iter(&tree, true);
	normal = benchmark_iter(&tree, false);

	printv(2, "Size: %8ld, step: %8ld, tagged iteration: %8lld ns\n",
		size, step, tagged);
	printv(2, "Size: %8ld, step: %8ld, normal iteration: %8lld ns\n",
		size, step, normal);

	benchmark_delete(&tree, size, step);

	item_kill_tree(&tree);
	rcu_barrier();
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
			benchmark_size(size[c], step[s]);
}
