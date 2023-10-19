// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * iteration_check_2.c: Check that deleting a tagged entry doesn't cause
 * an RCU walker to finish early.
 * Copyright (c) 2020 Oracle
 * Author: Matthew Wilcox <willy@infradead.org>
 */
#include <pthread.h>
#include "test.h"

static volatile bool test_complete;

static void *iterator(void *arg)
{
	XA_STATE(xas, arg, 0);
	void *entry;

	rcu_register_thread();

	while (!test_complete) {
		xas_set(&xas, 0);
		rcu_read_lock();
		xas_for_each_marked(&xas, entry, ULONG_MAX, XA_MARK_0)
			;
		rcu_read_unlock();
		assert(xas.xa_index >= 100);
	}

	rcu_unregister_thread();
	return NULL;
}

static void *throbber(void *arg)
{
	struct xarray *xa = arg;

	rcu_register_thread();

	while (!test_complete) {
		int i;

		for (i = 0; i < 100; i++) {
			xa_store(xa, i, xa_mk_value(i), GFP_KERNEL);
			xa_set_mark(xa, i, XA_MARK_0);
		}
		for (i = 0; i < 100; i++)
			xa_erase(xa, i);
	}

	rcu_unregister_thread();
	return NULL;
}

void iteration_test2(unsigned test_duration)
{
	pthread_t threads[2];
	DEFINE_XARRAY(array);
	int i;

	printv(1, "Running iteration test 2 for %d seconds\n", test_duration);

	test_complete = false;

	xa_store(&array, 100, xa_mk_value(100), GFP_KERNEL);
	xa_set_mark(&array, 100, XA_MARK_0);

	if (pthread_create(&threads[0], NULL, iterator, &array)) {
		perror("create iterator thread");
		exit(1);
	}
	if (pthread_create(&threads[1], NULL, throbber, &array)) {
		perror("create throbber thread");
		exit(1);
	}

	sleep(test_duration);
	test_complete = true;

	for (i = 0; i < 2; i++) {
		if (pthread_join(threads[i], NULL)) {
			perror("pthread_join");
			exit(1);
		}
	}

	xa_destroy(&array);
}
