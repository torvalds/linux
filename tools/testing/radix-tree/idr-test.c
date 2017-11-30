/*
 * idr-test.c: Test the IDR API
 * Copyright (c) 2016 Matthew Wilcox <willy@infradead.org>
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
#include <linux/bitmap.h>
#include <linux/idr.h>
#include <linux/slab.h>
#include <linux/kernel.h>
#include <linux/errno.h>

#include "test.h"

#define DUMMY_PTR	((void *)0x12)

int item_idr_free(int id, void *p, void *data)
{
	struct item *item = p;
	assert(item->index == id);
	free(p);

	return 0;
}

void item_idr_remove(struct idr *idr, int id)
{
	struct item *item = idr_find(idr, id);
	assert(item->index == id);
	idr_remove(idr, id);
	free(item);
}

void idr_alloc_test(void)
{
	unsigned long i;
	DEFINE_IDR(idr);

	assert(idr_alloc_cyclic(&idr, DUMMY_PTR, 0, 0x4000, GFP_KERNEL) == 0);
	assert(idr_alloc_cyclic(&idr, DUMMY_PTR, 0x3ffd, 0x4000, GFP_KERNEL) == 0x3ffd);
	idr_remove(&idr, 0x3ffd);
	idr_remove(&idr, 0);

	for (i = 0x3ffe; i < 0x4003; i++) {
		int id;
		struct item *item;

		if (i < 0x4000)
			item = item_create(i, 0);
		else
			item = item_create(i - 0x3fff, 0);

		id = idr_alloc_cyclic(&idr, item, 1, 0x4000, GFP_KERNEL);
		assert(id == item->index);
	}

	idr_for_each(&idr, item_idr_free, &idr);
	idr_destroy(&idr);
}

void idr_replace_test(void)
{
	DEFINE_IDR(idr);

	idr_alloc(&idr, (void *)-1, 10, 11, GFP_KERNEL);
	idr_replace(&idr, &idr, 10);

	idr_destroy(&idr);
}

/*
 * Unlike the radix tree, you can put a NULL pointer -- with care -- into
 * the IDR.  Some interfaces, like idr_find() do not distinguish between
 * "present, value is NULL" and "not present", but that's exactly what some
 * users want.
 */
void idr_null_test(void)
{
	int i;
	DEFINE_IDR(idr);

	assert(idr_is_empty(&idr));

	assert(idr_alloc(&idr, NULL, 0, 0, GFP_KERNEL) == 0);
	assert(!idr_is_empty(&idr));
	idr_remove(&idr, 0);
	assert(idr_is_empty(&idr));

	assert(idr_alloc(&idr, NULL, 0, 0, GFP_KERNEL) == 0);
	assert(!idr_is_empty(&idr));
	idr_destroy(&idr);
	assert(idr_is_empty(&idr));

	for (i = 0; i < 10; i++) {
		assert(idr_alloc(&idr, NULL, 0, 0, GFP_KERNEL) == i);
	}

	assert(idr_replace(&idr, DUMMY_PTR, 3) == NULL);
	assert(idr_replace(&idr, DUMMY_PTR, 4) == NULL);
	assert(idr_replace(&idr, NULL, 4) == DUMMY_PTR);
	assert(idr_replace(&idr, DUMMY_PTR, 11) == ERR_PTR(-ENOENT));
	idr_remove(&idr, 5);
	assert(idr_alloc(&idr, NULL, 0, 0, GFP_KERNEL) == 5);
	idr_remove(&idr, 5);

	for (i = 0; i < 9; i++) {
		idr_remove(&idr, i);
		assert(!idr_is_empty(&idr));
	}
	idr_remove(&idr, 8);
	assert(!idr_is_empty(&idr));
	idr_remove(&idr, 9);
	assert(idr_is_empty(&idr));

	assert(idr_alloc(&idr, NULL, 0, 0, GFP_KERNEL) == 0);
	assert(idr_replace(&idr, DUMMY_PTR, 3) == ERR_PTR(-ENOENT));
	assert(idr_replace(&idr, DUMMY_PTR, 0) == NULL);
	assert(idr_replace(&idr, NULL, 0) == DUMMY_PTR);

	idr_destroy(&idr);
	assert(idr_is_empty(&idr));

	for (i = 1; i < 10; i++) {
		assert(idr_alloc(&idr, NULL, 1, 0, GFP_KERNEL) == i);
	}

	idr_destroy(&idr);
	assert(idr_is_empty(&idr));
}

void idr_nowait_test(void)
{
	unsigned int i;
	DEFINE_IDR(idr);

	idr_preload(GFP_KERNEL);

	for (i = 0; i < 3; i++) {
		struct item *item = item_create(i, 0);
		assert(idr_alloc(&idr, item, i, i + 1, GFP_NOWAIT) == i);
	}

	idr_preload_end();

	idr_for_each(&idr, item_idr_free, &idr);
	idr_destroy(&idr);
}

void idr_get_next_test(int base)
{
	unsigned long i;
	int nextid;
	DEFINE_IDR(idr);
	idr_init_base(&idr, base);

	int indices[] = {4, 7, 9, 15, 65, 128, 1000, 99999, 0};

	for(i = 0; indices[i]; i++) {
		struct item *item = item_create(indices[i], 0);
		assert(idr_alloc(&idr, item, indices[i], indices[i+1],
				 GFP_KERNEL) == indices[i]);
	}

	for(i = 0, nextid = 0; indices[i]; i++) {
		idr_get_next(&idr, &nextid);
		assert(nextid == indices[i]);
		nextid++;
	}

	idr_for_each(&idr, item_idr_free, &idr);
	idr_destroy(&idr);
}

void idr_checks(void)
{
	unsigned long i;
	DEFINE_IDR(idr);

	for (i = 0; i < 10000; i++) {
		struct item *item = item_create(i, 0);
		assert(idr_alloc(&idr, item, 0, 20000, GFP_KERNEL) == i);
	}

	assert(idr_alloc(&idr, DUMMY_PTR, 5, 30, GFP_KERNEL) < 0);

	for (i = 0; i < 5000; i++)
		item_idr_remove(&idr, i);

	idr_remove(&idr, 3);

	idr_for_each(&idr, item_idr_free, &idr);
	idr_destroy(&idr);

	assert(idr_is_empty(&idr));

	idr_remove(&idr, 3);
	idr_remove(&idr, 0);

	for (i = INT_MAX - 3UL; i < INT_MAX + 1UL; i++) {
		struct item *item = item_create(i, 0);
		assert(idr_alloc(&idr, item, i, i + 10, GFP_KERNEL) == i);
	}
	assert(idr_alloc(&idr, DUMMY_PTR, i - 2, i, GFP_KERNEL) == -ENOSPC);
	assert(idr_alloc(&idr, DUMMY_PTR, i - 2, i + 10, GFP_KERNEL) == -ENOSPC);

	idr_for_each(&idr, item_idr_free, &idr);
	idr_destroy(&idr);
	idr_destroy(&idr);

	assert(idr_is_empty(&idr));

	idr_set_cursor(&idr, INT_MAX - 3UL);
	for (i = INT_MAX - 3UL; i < INT_MAX + 3UL; i++) {
		struct item *item;
		unsigned int id;
		if (i <= INT_MAX)
			item = item_create(i, 0);
		else
			item = item_create(i - INT_MAX - 1, 0);

		id = idr_alloc_cyclic(&idr, item, 0, 0, GFP_KERNEL);
		assert(id == item->index);
	}

	idr_for_each(&idr, item_idr_free, &idr);
	idr_destroy(&idr);
	assert(idr_is_empty(&idr));

	for (i = 1; i < 10000; i++) {
		struct item *item = item_create(i, 0);
		assert(idr_alloc(&idr, item, 1, 20000, GFP_KERNEL) == i);
	}

	idr_for_each(&idr, item_idr_free, &idr);
	idr_destroy(&idr);

	idr_replace_test();
	idr_alloc_test();
	idr_null_test();
	idr_nowait_test();
	idr_get_next_test(0);
	idr_get_next_test(1);
	idr_get_next_test(4);
}

/*
 * Check that we get the correct error when we run out of memory doing
 * allocations.  To ensure we run out of memory, just "forget" to preload.
 * The first test is for not having a bitmap available, and the second test
 * is for not being able to allocate a level of the radix tree.
 */
void ida_check_nomem(void)
{
	DEFINE_IDA(ida);
	int id, err;

	err = ida_get_new_above(&ida, 256, &id);
	assert(err == -EAGAIN);
	err = ida_get_new_above(&ida, 1UL << 30, &id);
	assert(err == -EAGAIN);
}

/*
 * Check what happens when we fill a leaf and then delete it.  This may
 * discover mishandling of IDR_FREE.
 */
void ida_check_leaf(void)
{
	DEFINE_IDA(ida);
	int id;
	unsigned long i;

	for (i = 0; i < IDA_BITMAP_BITS; i++) {
		assert(ida_pre_get(&ida, GFP_KERNEL));
		assert(!ida_get_new(&ida, &id));
		assert(id == i);
	}

	ida_destroy(&ida);
	assert(ida_is_empty(&ida));

	assert(ida_pre_get(&ida, GFP_KERNEL));
	assert(!ida_get_new(&ida, &id));
	assert(id == 0);
	ida_destroy(&ida);
	assert(ida_is_empty(&ida));
}

/*
 * Check handling of conversions between exceptional entries and full bitmaps.
 */
void ida_check_conv(void)
{
	DEFINE_IDA(ida);
	int id;
	unsigned long i;

	for (i = 0; i < IDA_BITMAP_BITS * 2; i += IDA_BITMAP_BITS) {
		assert(ida_pre_get(&ida, GFP_KERNEL));
		assert(!ida_get_new_above(&ida, i + 1, &id));
		assert(id == i + 1);
		assert(!ida_get_new_above(&ida, i + BITS_PER_LONG, &id));
		assert(id == i + BITS_PER_LONG);
		ida_remove(&ida, i + 1);
		ida_remove(&ida, i + BITS_PER_LONG);
		assert(ida_is_empty(&ida));
	}

	assert(ida_pre_get(&ida, GFP_KERNEL));

	for (i = 0; i < IDA_BITMAP_BITS * 2; i++) {
		assert(ida_pre_get(&ida, GFP_KERNEL));
		assert(!ida_get_new(&ida, &id));
		assert(id == i);
	}

	for (i = IDA_BITMAP_BITS * 2; i > 0; i--) {
		ida_remove(&ida, i - 1);
	}
	assert(ida_is_empty(&ida));

	for (i = 0; i < IDA_BITMAP_BITS + BITS_PER_LONG - 4; i++) {
		assert(ida_pre_get(&ida, GFP_KERNEL));
		assert(!ida_get_new(&ida, &id));
		assert(id == i);
	}

	for (i = IDA_BITMAP_BITS + BITS_PER_LONG - 4; i > 0; i--) {
		ida_remove(&ida, i - 1);
	}
	assert(ida_is_empty(&ida));

	radix_tree_cpu_dead(1);
	for (i = 0; i < 1000000; i++) {
		int err = ida_get_new(&ida, &id);
		if (err == -EAGAIN) {
			assert((i % IDA_BITMAP_BITS) == (BITS_PER_LONG - 2));
			assert(ida_pre_get(&ida, GFP_KERNEL));
			err = ida_get_new(&ida, &id);
		} else {
			assert((i % IDA_BITMAP_BITS) != (BITS_PER_LONG - 2));
		}
		assert(!err);
		assert(id == i);
	}
	ida_destroy(&ida);
}

/*
 * Check allocations up to and slightly above the maximum allowed (2^31-1) ID.
 * Allocating up to 2^31-1 should succeed, and then allocating the next one
 * should fail.
 */
void ida_check_max(void)
{
	DEFINE_IDA(ida);
	int id, err;
	unsigned long i, j;

	for (j = 1; j < 65537; j *= 2) {
		unsigned long base = (1UL << 31) - j;
		for (i = 0; i < j; i++) {
			assert(ida_pre_get(&ida, GFP_KERNEL));
			assert(!ida_get_new_above(&ida, base, &id));
			assert(id == base + i);
		}
		assert(ida_pre_get(&ida, GFP_KERNEL));
		err = ida_get_new_above(&ida, base, &id);
		assert(err == -ENOSPC);
		ida_destroy(&ida);
		assert(ida_is_empty(&ida));
		rcu_barrier();
	}
}

void ida_check_random(void)
{
	DEFINE_IDA(ida);
	DECLARE_BITMAP(bitmap, 2048);
	int id, err;
	unsigned int i;
	time_t s = time(NULL);

 repeat:
	memset(bitmap, 0, sizeof(bitmap));
	for (i = 0; i < 100000; i++) {
		int i = rand();
		int bit = i & 2047;
		if (test_bit(bit, bitmap)) {
			__clear_bit(bit, bitmap);
			ida_remove(&ida, bit);
		} else {
			__set_bit(bit, bitmap);
			do {
				ida_pre_get(&ida, GFP_KERNEL);
				err = ida_get_new_above(&ida, bit, &id);
			} while (err == -EAGAIN);
			assert(!err);
			assert(id == bit);
		}
	}
	ida_destroy(&ida);
	if (time(NULL) < s + 10)
		goto repeat;
}

void ida_simple_get_remove_test(void)
{
	DEFINE_IDA(ida);
	unsigned long i;

	for (i = 0; i < 10000; i++) {
		assert(ida_simple_get(&ida, 0, 20000, GFP_KERNEL) == i);
	}
	assert(ida_simple_get(&ida, 5, 30, GFP_KERNEL) < 0);

	for (i = 0; i < 10000; i++) {
		ida_simple_remove(&ida, i);
	}
	assert(ida_is_empty(&ida));

	ida_destroy(&ida);
}

void ida_checks(void)
{
	DEFINE_IDA(ida);
	int id;
	unsigned long i;

	radix_tree_cpu_dead(1);
	ida_check_nomem();

	for (i = 0; i < 10000; i++) {
		assert(ida_pre_get(&ida, GFP_KERNEL));
		assert(!ida_get_new(&ida, &id));
		assert(id == i);
	}

	ida_remove(&ida, 20);
	ida_remove(&ida, 21);
	for (i = 0; i < 3; i++) {
		assert(ida_pre_get(&ida, GFP_KERNEL));
		assert(!ida_get_new(&ida, &id));
		if (i == 2)
			assert(id == 10000);
	}

	for (i = 0; i < 5000; i++)
		ida_remove(&ida, i);

	assert(ida_pre_get(&ida, GFP_KERNEL));
	assert(!ida_get_new_above(&ida, 5000, &id));
	assert(id == 10001);

	ida_destroy(&ida);

	assert(ida_is_empty(&ida));

	assert(ida_pre_get(&ida, GFP_KERNEL));
	assert(!ida_get_new_above(&ida, 1, &id));
	assert(id == 1);

	ida_remove(&ida, id);
	assert(ida_is_empty(&ida));
	ida_destroy(&ida);
	assert(ida_is_empty(&ida));

	assert(ida_pre_get(&ida, GFP_KERNEL));
	assert(!ida_get_new_above(&ida, 1, &id));
	ida_destroy(&ida);
	assert(ida_is_empty(&ida));

	assert(ida_pre_get(&ida, GFP_KERNEL));
	assert(!ida_get_new_above(&ida, 1, &id));
	assert(id == 1);
	assert(ida_pre_get(&ida, GFP_KERNEL));
	assert(!ida_get_new_above(&ida, 1025, &id));
	assert(id == 1025);
	assert(ida_pre_get(&ida, GFP_KERNEL));
	assert(!ida_get_new_above(&ida, 10000, &id));
	assert(id == 10000);
	ida_remove(&ida, 1025);
	ida_destroy(&ida);
	assert(ida_is_empty(&ida));

	ida_check_leaf();
	ida_check_max();
	ida_check_conv();
	ida_check_random();
	ida_simple_get_remove_test();

	radix_tree_cpu_dead(1);
}

static void *ida_random_fn(void *arg)
{
	rcu_register_thread();
	ida_check_random();
	rcu_unregister_thread();
	return NULL;
}

void ida_thread_tests(void)
{
	pthread_t threads[20];
	int i;

	for (i = 0; i < ARRAY_SIZE(threads); i++)
		if (pthread_create(&threads[i], NULL, ida_random_fn, NULL)) {
			perror("creating ida thread");
			exit(1);
		}

	while (i--)
		pthread_join(threads[i], NULL);
}

int __weak main(void)
{
	radix_tree_init();
	idr_checks();
	ida_checks();
	ida_thread_tests();
	radix_tree_cpu_dead(1);
	rcu_barrier();
	if (nr_allocated)
		printf("nr_allocated = %d\n", nr_allocated);
	return 0;
}
