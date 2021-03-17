// SPDX-License-Identifier: GPL-2.0
#include <linux/kernel.h>
#include <linux/gfp.h>
#include <linux/slab.h>
#include <linux/radix-tree.h>
#include <linux/rcupdate.h>
#include <stdlib.h>
#include <pthread.h>
#include <stdio.h>
#include <assert.h>

#include "regression.h"

static pthread_barrier_t worker_barrier;
static int obj0, obj1;
static RADIX_TREE(mt_tree, GFP_KERNEL);

static void *reader_fn(void *arg)
{
	int i;
	void *entry;

	rcu_register_thread();
	pthread_barrier_wait(&worker_barrier);

	for (i = 0; i < 1000000; i++) {
		rcu_read_lock();
		entry = radix_tree_lookup(&mt_tree, 0);
		rcu_read_unlock();
		if (entry != &obj0) {
			printf("iteration %d bad entry = %p\n", i, entry);
			abort();
		}
	}

	rcu_unregister_thread();

	return NULL;
}

static void *writer_fn(void *arg)
{
	int i;

	rcu_register_thread();
	pthread_barrier_wait(&worker_barrier);

	for (i = 0; i < 1000000; i++) {
		radix_tree_insert(&mt_tree, 1, &obj1);
		radix_tree_delete(&mt_tree, 1);
	}

	rcu_unregister_thread();

	return NULL;
}

void regression4_test(void)
{
	pthread_t reader, writer;

	printv(1, "regression test 4 starting\n");

	radix_tree_insert(&mt_tree, 0, &obj0);
	pthread_barrier_init(&worker_barrier, NULL, 2);

	if (pthread_create(&reader, NULL, reader_fn, NULL) ||
	    pthread_create(&writer, NULL, writer_fn, NULL)) {
		perror("pthread_create");
		exit(1);
	}

	if (pthread_join(reader, NULL) || pthread_join(writer, NULL)) {
		perror("pthread_join");
		exit(1);
	}

	printv(1, "regression test 4 passed\n");
}
