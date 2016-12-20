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

	idr_for_each(&idr, item_idr_free, &idr);
	idr_destroy(&idr);
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

	err = ida_get_new(&ida, &id);
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

	radix_tree_cpu_dead(1);
}
