// SPDX-License-Identifier: GPL-2.0-only
/*
 * iteration_check.c: test races having to do with xarray iteration
 * Copyright (c) 2016 Intel Corporation
 * Author: Ross Zwisler <ross.zwisler@linux.intel.com>
 */
#include <pthread.h>
#include "test.h"

#define NUM_THREADS	5
#define MAX_IDX		100
#define TAG		XA_MARK_0
#define NEW_TAG		XA_MARK_1

static pthread_t threads[NUM_THREADS];
static unsigned int seeds[3];
static DEFINE_XARRAY(array);
static bool test_complete;
static int max_order;

void my_item_insert(struct xarray *xa, unsigned long index)
{
	XA_STATE(xas, xa, index);
	struct item *item = item_create(index, 0);
	int order;

retry:
	xas_lock(&xas);
	for (order = max_order; order >= 0; order--) {
		xas_set_order(&xas, index, order);
		item->order = order;
		if (xas_find_conflict(&xas))
			continue;
		xas_store(&xas, item);
		xas_set_mark(&xas, TAG);
		break;
	}
	xas_unlock(&xas);
	if (xas_nomem(&xas, GFP_KERNEL))
		goto retry;
	if (order < 0)
		free(item);
}

/* relentlessly fill the array with tagged entries */
static void *add_entries_fn(void *arg)
{
	rcu_register_thread();

	while (!test_complete) {
		unsigned long pgoff;

		for (pgoff = 0; pgoff < MAX_IDX; pgoff++) {
			my_item_insert(&array, pgoff);
		}
	}

	rcu_unregister_thread();

	return NULL;
}

/*
 * Iterate over tagged entries, retrying when we find ourselves in a deleted
 * node and randomly pausing the iteration.
 */
static void *tagged_iteration_fn(void *arg)
{
	XA_STATE(xas, &array, 0);
	void *entry;

	rcu_register_thread();

	while (!test_complete) {
		xas_set(&xas, 0);
		rcu_read_lock();
		xas_for_each_marked(&xas, entry, ULONG_MAX, TAG) {
			if (xas_retry(&xas, entry))
				continue;

			if (rand_r(&seeds[0]) % 50 == 0) {
				xas_pause(&xas);
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
 * Iterate over the entries, retrying when we find ourselves in a deleted
 * node and randomly pausing the iteration.
 */
static void *untagged_iteration_fn(void *arg)
{
	XA_STATE(xas, &array, 0);
	void *entry;

	rcu_register_thread();

	while (!test_complete) {
		xas_set(&xas, 0);
		rcu_read_lock();
		xas_for_each(&xas, entry, ULONG_MAX) {
			if (xas_retry(&xas, entry))
				continue;

			if (rand_r(&seeds[1]) % 50 == 0) {
				xas_pause(&xas);
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
 * Randomly remove entries to help induce retries in the
 * two iteration functions.
 */
static void *remove_entries_fn(void *arg)
{
	rcu_register_thread();

	while (!test_complete) {
		int pgoff;
		struct item *item;

		pgoff = rand_r(&seeds[2]) % MAX_IDX;

		item = xa_erase(&array, pgoff);
		if (item)
			item_free(item, pgoff);
	}

	rcu_unregister_thread();

	return NULL;
}

static void *tag_entries_fn(void *arg)
{
	rcu_register_thread();

	while (!test_complete) {
		tag_tagged_items(&array, 0, MAX_IDX, 10, TAG, NEW_TAG);
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

	item_kill_tree(&array);
}
