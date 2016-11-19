/*
 * iteration_check.c: test races having to do with radix tree iteration
 * Copyright (c) 2016 Intel Corporation
 * Author: Ross Zwisler <ross.zwisler@linux.intel.com>
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
#include <pthread.h>
#include "test.h"

#define NUM_THREADS 4
#define TAG 0
static pthread_mutex_t tree_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_t threads[NUM_THREADS];
RADIX_TREE(tree, GFP_KERNEL);
bool test_complete;

/* relentlessly fill the tree with tagged entries */
static void *add_entries_fn(void *arg)
{
	int pgoff;

	while (!test_complete) {
		for (pgoff = 0; pgoff < 100; pgoff++) {
			pthread_mutex_lock(&tree_lock);
			if (item_insert(&tree, pgoff) == 0)
				item_tag_set(&tree, pgoff, TAG);
			pthread_mutex_unlock(&tree_lock);
		}
	}

	return NULL;
}

/*
 * Iterate over the tagged entries, doing a radix_tree_iter_retry() as we find
 * things that have been removed and randomly resetting our iteration to the
 * next chunk with radix_tree_iter_next().  Both radix_tree_iter_retry() and
 * radix_tree_iter_next() cause radix_tree_next_slot() to be called with a
 * NULL 'slot' variable.
 */
static void *tagged_iteration_fn(void *arg)
{
	struct radix_tree_iter iter;
	void **slot;

	while (!test_complete) {
		rcu_read_lock();
		radix_tree_for_each_tagged(slot, &tree, &iter, 0, TAG) {
			void *entry;
			int i;

			/* busy wait to let removals happen */
			for (i = 0; i < 1000000; i++)
				;

			entry = radix_tree_deref_slot(slot);
			if (unlikely(!entry))
				continue;

			if (radix_tree_deref_retry(entry)) {
				slot = radix_tree_iter_retry(&iter);
				continue;
			}

			if (rand() % 50 == 0)
				slot = radix_tree_iter_next(&iter);
		}
		rcu_read_unlock();
	}

	return NULL;
}

/*
 * Iterate over the entries, doing a radix_tree_iter_retry() as we find things
 * that have been removed and randomly resetting our iteration to the next
 * chunk with radix_tree_iter_next().  Both radix_tree_iter_retry() and
 * radix_tree_iter_next() cause radix_tree_next_slot() to be called with a
 * NULL 'slot' variable.
 */
static void *untagged_iteration_fn(void *arg)
{
	struct radix_tree_iter iter;
	void **slot;

	while (!test_complete) {
		rcu_read_lock();
		radix_tree_for_each_slot(slot, &tree, &iter, 0) {
			void *entry;
			int i;

			/* busy wait to let removals happen */
			for (i = 0; i < 1000000; i++)
				;

			entry = radix_tree_deref_slot(slot);
			if (unlikely(!entry))
				continue;

			if (radix_tree_deref_retry(entry)) {
				slot = radix_tree_iter_retry(&iter);
				continue;
			}

			if (rand() % 50 == 0)
				slot = radix_tree_iter_next(&iter);
		}
		rcu_read_unlock();
	}

	return NULL;
}

/*
 * Randomly remove entries to help induce radix_tree_iter_retry() calls in the
 * two iteration functions.
 */
static void *remove_entries_fn(void *arg)
{
	while (!test_complete) {
		int pgoff;

		pgoff = rand() % 100;

		pthread_mutex_lock(&tree_lock);
		item_delete(&tree, pgoff);
		pthread_mutex_unlock(&tree_lock);
	}

	return NULL;
}

/* This is a unit test for a bug found by the syzkaller tester */
void iteration_test(void)
{
	int i;

	printf("Running iteration tests for 10 seconds\n");

	srand(time(0));
	test_complete = false;

	if (pthread_create(&threads[0], NULL, tagged_iteration_fn, NULL)) {
		perror("pthread_create");
		exit(1);
	}
	if (pthread_create(&threads[1], NULL, untagged_iteration_fn, NULL)) {
		perror("pthread_create");
		exit(1);
	}
	if (pthread_create(&threads[2], NULL, add_entries_fn, NULL)) {
		perror("pthread_create");
		exit(1);
	}
	if (pthread_create(&threads[3], NULL, remove_entries_fn, NULL)) {
		perror("pthread_create");
		exit(1);
	}

	sleep(10);
	test_complete = true;

	for (i = 0; i < NUM_THREADS; i++) {
		if (pthread_join(threads[i], NULL)) {
			perror("pthread_join");
			exit(1);
		}
	}

	item_kill_tree(&tree);
}
