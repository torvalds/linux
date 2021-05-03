// SPDX-License-Identifier: GPL-2.0-only
/*
 * idr-test.c: Test the IDR API
 * Copyright (c) 2016 Matthew Wilcox <willy@infradead.org>
 */
#include <linux/bitmap.h>
#include <linux/idr.h>
#include <linux/slab.h>
#include <linux/kernel.h>
#include <linux/errno.h>

#include "test.h"

#define DUMMY_PTR	((void *)0x10)

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

int idr_u32_cb(int id, void *ptr, void *data)
{
	BUG_ON(id < 0);
	BUG_ON(ptr != DUMMY_PTR);
	return 0;
}

void idr_u32_test1(struct idr *idr, u32 handle)
{
	static bool warned = false;
	u32 id = handle;
	int sid = 0;
	void *ptr;

	BUG_ON(idr_alloc_u32(idr, DUMMY_PTR, &id, id, GFP_KERNEL));
	BUG_ON(id != handle);
	BUG_ON(idr_alloc_u32(idr, DUMMY_PTR, &id, id, GFP_KERNEL) != -ENOSPC);
	BUG_ON(id != handle);
	if (!warned && id > INT_MAX)
		printk("vvv Ignore these warnings\n");
	ptr = idr_get_next(idr, &sid);
	if (id > INT_MAX) {
		BUG_ON(ptr != NULL);
		BUG_ON(sid != 0);
	} else {
		BUG_ON(ptr != DUMMY_PTR);
		BUG_ON(sid != id);
	}
	idr_for_each(idr, idr_u32_cb, NULL);
	if (!warned && id > INT_MAX) {
		printk("^^^ Warnings over\n");
		warned = true;
	}
	BUG_ON(idr_remove(idr, id) != DUMMY_PTR);
	BUG_ON(!idr_is_empty(idr));
}

void idr_u32_test(int base)
{
	DEFINE_IDR(idr);
	idr_init_base(&idr, base);
	idr_u32_test1(&idr, 10);
	idr_u32_test1(&idr, 0x7fffffff);
	idr_u32_test1(&idr, 0x80000000);
	idr_u32_test1(&idr, 0x80000001);
	idr_u32_test1(&idr, 0xffe00000);
	idr_u32_test1(&idr, 0xffffffff);
}

static void idr_align_test(struct idr *idr)
{
	char name[] = "Motorola 68000";
	int i, id;
	void *entry;

	for (i = 0; i < 9; i++) {
		BUG_ON(idr_alloc(idr, &name[i], 0, 0, GFP_KERNEL) != i);
		idr_for_each_entry(idr, entry, id);
	}
	idr_destroy(idr);

	for (i = 1; i < 10; i++) {
		BUG_ON(idr_alloc(idr, &name[i], 0, 0, GFP_KERNEL) != i - 1);
		idr_for_each_entry(idr, entry, id);
	}
	idr_destroy(idr);

	for (i = 2; i < 11; i++) {
		BUG_ON(idr_alloc(idr, &name[i], 0, 0, GFP_KERNEL) != i - 2);
		idr_for_each_entry(idr, entry, id);
	}
	idr_destroy(idr);

	for (i = 3; i < 12; i++) {
		BUG_ON(idr_alloc(idr, &name[i], 0, 0, GFP_KERNEL) != i - 3);
		idr_for_each_entry(idr, entry, id);
	}
	idr_destroy(idr);

	for (i = 0; i < 8; i++) {
		BUG_ON(idr_alloc(idr, &name[i], 0, 0, GFP_KERNEL) != 0);
		BUG_ON(idr_alloc(idr, &name[i + 1], 0, 0, GFP_KERNEL) != 1);
		idr_for_each_entry(idr, entry, id);
		idr_remove(idr, 1);
		idr_for_each_entry(idr, entry, id);
		idr_remove(idr, 0);
		BUG_ON(!idr_is_empty(idr));
	}

	for (i = 0; i < 8; i++) {
		BUG_ON(idr_alloc(idr, NULL, 0, 0, GFP_KERNEL) != 0);
		idr_for_each_entry(idr, entry, id);
		idr_replace(idr, &name[i], 0);
		idr_for_each_entry(idr, entry, id);
		BUG_ON(idr_find(idr, 0) != &name[i]);
		idr_remove(idr, 0);
	}

	for (i = 0; i < 8; i++) {
		BUG_ON(idr_alloc(idr, &name[i], 0, 0, GFP_KERNEL) != 0);
		BUG_ON(idr_alloc(idr, NULL, 0, 0, GFP_KERNEL) != 1);
		idr_remove(idr, 1);
		idr_for_each_entry(idr, entry, id);
		idr_replace(idr, &name[i + 1], 0);
		idr_for_each_entry(idr, entry, id);
		idr_remove(idr, 0);
	}
}

DEFINE_IDR(find_idr);

static void *idr_throbber(void *arg)
{
	time_t start = time(NULL);
	int id = *(int *)arg;

	rcu_register_thread();
	do {
		idr_alloc(&find_idr, xa_mk_value(id), id, id + 1, GFP_KERNEL);
		idr_remove(&find_idr, id);
	} while (time(NULL) < start + 10);
	rcu_unregister_thread();

	return NULL;
}

void idr_find_test_1(int anchor_id, int throbber_id)
{
	pthread_t throbber;
	time_t start = time(NULL);

	BUG_ON(idr_alloc(&find_idr, xa_mk_value(anchor_id), anchor_id,
				anchor_id + 1, GFP_KERNEL) != anchor_id);

	pthread_create(&throbber, NULL, idr_throbber, &throbber_id);

	rcu_read_lock();
	do {
		int id = 0;
		void *entry = idr_get_next(&find_idr, &id);
		rcu_read_unlock();
		BUG_ON(entry != xa_mk_value(id));
		rcu_read_lock();
	} while (time(NULL) < start + 11);
	rcu_read_unlock();

	pthread_join(throbber, NULL);

	idr_remove(&find_idr, anchor_id);
	BUG_ON(!idr_is_empty(&find_idr));
}

void idr_find_test(void)
{
	idr_find_test_1(100000, 0);
	idr_find_test_1(0, 100000);
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

	assert(idr_alloc(&idr, DUMMY_PTR, 0, 0, GFP_KERNEL) == 0);
	idr_remove(&idr, 1);
	for (i = 1; i < RADIX_TREE_MAP_SIZE; i++)
		assert(idr_alloc(&idr, DUMMY_PTR, 0, 0, GFP_KERNEL) == i);
	idr_remove(&idr, 1 << 30);
	idr_destroy(&idr);

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
	idr_u32_test(4);
	idr_u32_test(1);
	idr_u32_test(0);
	idr_align_test(&idr);
	idr_find_test();
}

#define module_init(x)
#define module_exit(x)
#define MODULE_AUTHOR(x)
#define MODULE_LICENSE(x)
#define dump_stack()    assert(0)
void ida_dump(struct ida *);

#include "../../../lib/test_ida.c"

/*
 * Check that we get the correct error when we run out of memory doing
 * allocations.  In userspace, GFP_NOWAIT will always fail an allocation.
 * The first test is for not having a bitmap available, and the second test
 * is for not being able to allocate a level of the radix tree.
 */
void ida_check_nomem(void)
{
	DEFINE_IDA(ida);
	int id;

	id = ida_alloc_min(&ida, 256, GFP_NOWAIT);
	IDA_BUG_ON(&ida, id != -ENOMEM);
	id = ida_alloc_min(&ida, 1UL << 30, GFP_NOWAIT);
	IDA_BUG_ON(&ida, id != -ENOMEM);
	IDA_BUG_ON(&ida, !ida_is_empty(&ida));
}

/*
 * Check handling of conversions between exceptional entries and full bitmaps.
 */
void ida_check_conv_user(void)
{
	DEFINE_IDA(ida);
	unsigned long i;

	for (i = 0; i < 1000000; i++) {
		int id = ida_alloc(&ida, GFP_NOWAIT);
		if (id == -ENOMEM) {
			IDA_BUG_ON(&ida, ((i % IDA_BITMAP_BITS) !=
					  BITS_PER_XA_VALUE) &&
					 ((i % IDA_BITMAP_BITS) != 0));
			id = ida_alloc(&ida, GFP_KERNEL);
		} else {
			IDA_BUG_ON(&ida, (i % IDA_BITMAP_BITS) ==
					BITS_PER_XA_VALUE);
		}
		IDA_BUG_ON(&ida, id != i);
	}
	ida_destroy(&ida);
}

void ida_check_random(void)
{
	DEFINE_IDA(ida);
	DECLARE_BITMAP(bitmap, 2048);
	unsigned int i;
	time_t s = time(NULL);

 repeat:
	memset(bitmap, 0, sizeof(bitmap));
	for (i = 0; i < 100000; i++) {
		int i = rand();
		int bit = i & 2047;
		if (test_bit(bit, bitmap)) {
			__clear_bit(bit, bitmap);
			ida_free(&ida, bit);
		} else {
			__set_bit(bit, bitmap);
			IDA_BUG_ON(&ida, ida_alloc_min(&ida, bit, GFP_KERNEL)
					!= bit);
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

void user_ida_checks(void)
{
	radix_tree_cpu_dead(1);

	ida_check_nomem();
	ida_check_conv_user();
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

static void *ida_leak_fn(void *arg)
{
	struct ida *ida = arg;
	time_t s = time(NULL);
	int i, ret;

	rcu_register_thread();

	do for (i = 0; i < 1000; i++) {
		ret = ida_alloc_range(ida, 128, 128, GFP_KERNEL);
		if (ret >= 0)
			ida_free(ida, 128);
	} while (time(NULL) < s + 2);

	rcu_unregister_thread();
	return NULL;
}

void ida_thread_tests(void)
{
	DEFINE_IDA(ida);
	pthread_t threads[20];
	int i;

	for (i = 0; i < ARRAY_SIZE(threads); i++)
		if (pthread_create(&threads[i], NULL, ida_random_fn, NULL)) {
			perror("creating ida thread");
			exit(1);
		}

	while (i--)
		pthread_join(threads[i], NULL);

	for (i = 0; i < ARRAY_SIZE(threads); i++)
		if (pthread_create(&threads[i], NULL, ida_leak_fn, &ida)) {
			perror("creating ida thread");
			exit(1);
		}

	while (i--)
		pthread_join(threads[i], NULL);
	assert(ida_is_empty(&ida));
}

void ida_tests(void)
{
	user_ida_checks();
	ida_checks();
	ida_exit();
	ida_thread_tests();
}

int __weak main(void)
{
	rcu_register_thread();
	radix_tree_init();
	idr_checks();
	ida_tests();
	radix_tree_cpu_dead(1);
	rcu_barrier();
	if (nr_allocated)
		printf("nr_allocated = %d\n", nr_allocated);
	rcu_unregister_thread();
	return 0;
}
