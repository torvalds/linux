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

#define NUM_THREADS	5
#define MAX_IDX		100
#define TAG		0
#define NEW_TAG		1

static pthread_mutex_t tree_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_t threads[NUM_THREADS];
static unsigned int seeds[3];
static RADIX_TREE(tree, GFP_KERNEL);
static bool test_complete;
static int max_order;

/* relentlessly fill the tree with tagged entries */
static void *add_entries_fn(void *arg)
{
	rcu_register_thread();

	while (!test_complete) {
		unsigned long pgoff;
		int order;

		for (pgoff = 0; pgoff < MAX_IDX; pgoff++) {
			pthread_mutex_lock(&tree_lock);
			for (order = max_order; order >= 0; order--) {
				if (item_insert_order(&tree, pgoff, order)
						== 0) {
					item_tag_set(&tree, pgoff, TAG);
					break;
				}
			}
			pthread_mutex_unlock(&tree_lock);
		}
	}

	rcu_unregister_thread();

	return NULL;
}

/*
 * Iterate over the tagged entries, doing a radix_tree_iter_retry() as we find
 * things that have been removed and randomly resetting our iteration to the
 * next chunk with radix_tree_iter_resume().  Both radix_tree_iter_retry() and
 * radix_tree_iter_resume() cause radix_tree_next_slot() to be called with a
 * NULL 'slot' variable.
 */
static void *tagged_iteration_fn(void *arg)
{
	struct radix_tree_iter iter;
	void **slot;

	rcu_register_thread();

	while (!test_complete) {
		rcu_read_lock();
		radix_tree_for_each_tagged(slot, &tree, &iter, 0, TAG) {
			void *entry = radix_tree_deref_slot(slot);
			if (unlikely(!entry))
				continue;

			if (radix_tree_deref_retry(entry)) {
				slot = radix_tree_iter_retry(&iter);
				continue;
			}

			if (rand_r(&seeds[0]) % 50 == 0) {
				slot = radix_tree_iter_resume(slot, &iter);
				rcu_read_unlock();
				rcu_barrier();
				rcu_read_lock();
			}
		}
		rcu_read_unlock();
	}

	rcu_unregister_thread();

	return NULL;
}

/*
 * Iterate over the entries, doing a radix_tree_iter_retry() as we find things
 * that have been removed and randomly resetting our iteration to the next
 * chunk with radix_tree_iter_resume().  Both radix_tree_iter_retry() and
 * radix_tree_iter_resume() cause radix_tree_next_slot() to be called with a
 * NULL 'slot' variable.
 */
static void *untagged_iteration_fn(void *arg)
{
	struct radix_tree_iter iter;
	void **slot;

	rcu_register_thread();

	while (!test_complete) {
		rcu_read_lock();
		radix_tree_for_each_slot(slot, &tree, &iter, 0) {
			void *entry = radix_tree_deref_slot(slot);
			if (unlikely(!entry))
				continue;

			if (radix_tree_deref_retry(entry)) {
				slot = radix_tree_iter_retry(&iter);
				continue;
			}

			if (rand_r(&seeds[1]) % 50 == 0) {
				slot = radix_tree_iter_resume(slot, &iter);
				rcu_read_unlock();
				rcu_barrier();
				rcu_read_lock();
			}
		}
		rcu_read_unlock();
	}

	rcu_unregister_thread();

	return NULL;
}

/*
 * Randomly remove entries to help induce radix_tree_iter_retry() calls in the
 * two iteration functions.
 */
static void *remove_entries_fn(void *arg)
{
	rcu_register_thread();

	while (!test_complete) {
		int pgoff;

		pgoff = rand_r(&seeds[2]) % MAX_IDX;

		pthread_mutex_lock(&tree_lock);
		item_delete(&tree, pgoff);
		pthread_mutex_unlock(&tree_lock);
	}

	rcu_unregister_thread();

	return NULL;
}

static void *tag_entries_fn(void *arg)
{
	rcu_register_thread();

	while (!test_complete) {
		tag_tagged_items(&tree, &tree_lock, 0, MAX_IDX, 10, TAG,
					NEW_TAG);
	}
	rcu_unregister_thread();
	return NULL;
}

/* This is a unit test for a bug found by the syzkaller tester */
void iteration_test(unsigned order, unsigned test_duration)
{
	int i;

	printv(1, "Running %siteration tests for %d seconds\n",
			order > 0 ? "multiorder " : "", test_duration);

	max_order = order;
	test_complete = false;

	for (i = 0; i < 3; i++)
		seeds[i] = rand();

	if (pthread_create(&threads[0], NULL, tagged_iteration_fn, NULL)) {
		perror("create tagged iteration thread");
		exit(1);
	}
	if (pthread_create(&threads[1], NULL, untagged_iteration_fn, NULL)) {
		perror("create untagged iteration thread");
		exit(1);
	}
	if (pthread_create(&threads[2], NULL, add_entries_fn, NULL)) {
		perror("create add entry thread");
		exit(1);
	}
	if (pthread_create(&threads[3], NULL, remove_entries_fn, NULL)) {
		perror("create remove entry thread");
		exit(1);
	}
	if (pthread_create(&threads[4], NULL, tag_entries_fn, NULL)) {
		perror("create tag entry thread");
		exit(1);
	}

	sleep(test_duration);
	test_complete = true;

	for (i = 0; i < NUM_THREADS; i++) {
		if (pthread_join(threads[i], NULL)) {
			perror("pthread_join");
			exit(1);
		}
	}

	item_kill_tree(&tree);
}
