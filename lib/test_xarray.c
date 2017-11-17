// SPDX-License-Identifier: GPL-2.0+
/*
 * test_xarray.c: Test the XArray API
 * Copyright (c) 2017-2018 Microsoft Corporation
 * Author: Matthew Wilcox <willy@infradead.org>
 */

#include <linux/xarray.h>
#include <linux/module.h>

static unsigned int tests_run;
static unsigned int tests_passed;

#ifndef XA_DEBUG
# ifdef __KERNEL__
void xa_dump(const struct xarray *xa) { }
# endif
#undef XA_BUG_ON
#define XA_BUG_ON(xa, x) do {					\
	tests_run++;						\
	if (x) {						\
		printk("BUG at %s:%d\n", __func__, __LINE__);	\
		xa_dump(xa);					\
		dump_stack();					\
	} else {						\
		tests_passed++;					\
	}							\
} while (0)
#endif

static void *xa_store_index(struct xarray *xa, unsigned long index, gfp_t gfp)
{
	return xa_store(xa, index, xa_mk_value(index & LONG_MAX), gfp);
}

static void xa_erase_index(struct xarray *xa, unsigned long index)
{
	XA_BUG_ON(xa, xa_erase(xa, index) != xa_mk_value(index & LONG_MAX));
	XA_BUG_ON(xa, xa_load(xa, index) != NULL);
}

/*
 * If anyone needs this, please move it to xarray.c.  We have no current
 * users outside the test suite because all current multislot users want
 * to use the advanced API.
 */
static void *xa_store_order(struct xarray *xa, unsigned long index,
		unsigned order, void *entry, gfp_t gfp)
{
	XA_STATE_ORDER(xas, xa, index, order);
	void *curr;

	do {
		xas_lock(&xas);
		curr = xas_store(&xas, entry);
		xas_unlock(&xas);
	} while (xas_nomem(&xas, gfp));

	return curr;
}

static noinline void check_xa_err(struct xarray *xa)
{
	XA_BUG_ON(xa, xa_err(xa_store_index(xa, 0, GFP_NOWAIT)) != 0);
	XA_BUG_ON(xa, xa_err(xa_erase(xa, 0)) != 0);
#ifndef __KERNEL__
	/* The kernel does not fail GFP_NOWAIT allocations */
	XA_BUG_ON(xa, xa_err(xa_store_index(xa, 1, GFP_NOWAIT)) != -ENOMEM);
	XA_BUG_ON(xa, xa_err(xa_store_index(xa, 1, GFP_NOWAIT)) != -ENOMEM);
#endif
	XA_BUG_ON(xa, xa_err(xa_store_index(xa, 1, GFP_KERNEL)) != 0);
	XA_BUG_ON(xa, xa_err(xa_store(xa, 1, xa_mk_value(0), GFP_KERNEL)) != 0);
	XA_BUG_ON(xa, xa_err(xa_erase(xa, 1)) != 0);
// kills the test-suite :-(
//	XA_BUG_ON(xa, xa_err(xa_store(xa, 0, xa_mk_internal(0), 0)) != -EINVAL);
}

static noinline void check_xas_retry(struct xarray *xa)
{
	XA_STATE(xas, xa, 0);
	void *entry;

	xa_store_index(xa, 0, GFP_KERNEL);
	xa_store_index(xa, 1, GFP_KERNEL);

	rcu_read_lock();
	XA_BUG_ON(xa, xas_find(&xas, ULONG_MAX) != xa_mk_value(0));
	xa_erase_index(xa, 1);
	XA_BUG_ON(xa, !xa_is_retry(xas_reload(&xas)));
	XA_BUG_ON(xa, xas_retry(&xas, NULL));
	XA_BUG_ON(xa, xas_retry(&xas, xa_mk_value(0)));
	xas_reset(&xas);
	XA_BUG_ON(xa, xas.xa_node != XAS_RESTART);
	XA_BUG_ON(xa, xas_next_entry(&xas, ULONG_MAX) != xa_mk_value(0));
	XA_BUG_ON(xa, xas.xa_node != NULL);

	XA_BUG_ON(xa, xa_store_index(xa, 1, GFP_KERNEL) != NULL);
	XA_BUG_ON(xa, !xa_is_internal(xas_reload(&xas)));
	xas.xa_node = XAS_RESTART;
	XA_BUG_ON(xa, xas_next_entry(&xas, ULONG_MAX) != xa_mk_value(0));
	rcu_read_unlock();

	/* Make sure we can iterate through retry entries */
	xas_lock(&xas);
	xas_set(&xas, 0);
	xas_store(&xas, XA_RETRY_ENTRY);
	xas_set(&xas, 1);
	xas_store(&xas, XA_RETRY_ENTRY);

	xas_set(&xas, 0);
	xas_for_each(&xas, entry, ULONG_MAX) {
		xas_store(&xas, xa_mk_value(xas.xa_index));
	}
	xas_unlock(&xas);

	xa_erase_index(xa, 0);
	xa_erase_index(xa, 1);
}

static noinline void check_xa_load(struct xarray *xa)
{
	unsigned long i, j;

	for (i = 0; i < 1024; i++) {
		for (j = 0; j < 1024; j++) {
			void *entry = xa_load(xa, j);
			if (j < i)
				XA_BUG_ON(xa, xa_to_value(entry) != j);
			else
				XA_BUG_ON(xa, entry);
		}
		XA_BUG_ON(xa, xa_store_index(xa, i, GFP_KERNEL) != NULL);
	}

	for (i = 0; i < 1024; i++) {
		for (j = 0; j < 1024; j++) {
			void *entry = xa_load(xa, j);
			if (j >= i)
				XA_BUG_ON(xa, xa_to_value(entry) != j);
			else
				XA_BUG_ON(xa, entry);
		}
		xa_erase_index(xa, i);
	}
	XA_BUG_ON(xa, !xa_empty(xa));
}

static noinline void check_xa_mark_1(struct xarray *xa, unsigned long index)
{
	unsigned int order;
	unsigned int max_order = IS_ENABLED(CONFIG_XARRAY_MULTI) ? 8 : 1;

	/* NULL elements have no marks set */
	XA_BUG_ON(xa, xa_get_mark(xa, index, XA_MARK_0));
	xa_set_mark(xa, index, XA_MARK_0);
	XA_BUG_ON(xa, xa_get_mark(xa, index, XA_MARK_0));

	/* Storing a pointer will not make a mark appear */
	XA_BUG_ON(xa, xa_store_index(xa, index, GFP_KERNEL) != NULL);
	XA_BUG_ON(xa, xa_get_mark(xa, index, XA_MARK_0));
	xa_set_mark(xa, index, XA_MARK_0);
	XA_BUG_ON(xa, !xa_get_mark(xa, index, XA_MARK_0));

	/* Setting one mark will not set another mark */
	XA_BUG_ON(xa, xa_get_mark(xa, index + 1, XA_MARK_0));
	XA_BUG_ON(xa, xa_get_mark(xa, index, XA_MARK_1));

	/* Storing NULL clears marks, and they can't be set again */
	xa_erase_index(xa, index);
	XA_BUG_ON(xa, !xa_empty(xa));
	XA_BUG_ON(xa, xa_get_mark(xa, index, XA_MARK_0));
	xa_set_mark(xa, index, XA_MARK_0);
	XA_BUG_ON(xa, xa_get_mark(xa, index, XA_MARK_0));

	/*
	 * Storing a multi-index entry over entries with marks gives the
	 * entire entry the union of the marks
	 */
	BUG_ON((index % 4) != 0);
	for (order = 2; order < max_order; order++) {
		unsigned long base = round_down(index, 1UL << order);
		unsigned long next = base + (1UL << order);
		unsigned long i;

		XA_BUG_ON(xa, xa_store_index(xa, index + 1, GFP_KERNEL));
		xa_set_mark(xa, index + 1, XA_MARK_0);
		XA_BUG_ON(xa, xa_store_index(xa, index + 2, GFP_KERNEL));
		xa_set_mark(xa, index + 2, XA_MARK_1);
		XA_BUG_ON(xa, xa_store_index(xa, next, GFP_KERNEL));
		xa_store_order(xa, index, order, xa_mk_value(index),
				GFP_KERNEL);
		for (i = base; i < next; i++) {
			XA_BUG_ON(xa, !xa_get_mark(xa, i, XA_MARK_0));
			XA_BUG_ON(xa, !xa_get_mark(xa, i, XA_MARK_1));
			XA_BUG_ON(xa, xa_get_mark(xa, i, XA_MARK_2));
		}
		XA_BUG_ON(xa, xa_get_mark(xa, next, XA_MARK_0));
		XA_BUG_ON(xa, xa_get_mark(xa, next, XA_MARK_1));
		XA_BUG_ON(xa, xa_get_mark(xa, next, XA_MARK_2));
		xa_erase_index(xa, index);
		xa_erase_index(xa, next);
		XA_BUG_ON(xa, !xa_empty(xa));
	}
	XA_BUG_ON(xa, !xa_empty(xa));
}

static noinline void check_xa_mark(struct xarray *xa)
{
	unsigned long index;

	for (index = 0; index < 16384; index += 4)
		check_xa_mark_1(xa, index);
}

static noinline void check_xa_shrink(struct xarray *xa)
{
	XA_STATE(xas, xa, 1);
	struct xa_node *node;

	XA_BUG_ON(xa, !xa_empty(xa));
	XA_BUG_ON(xa, xa_store_index(xa, 0, GFP_KERNEL) != NULL);
	XA_BUG_ON(xa, xa_store_index(xa, 1, GFP_KERNEL) != NULL);

	/*
	 * Check that erasing the entry at 1 shrinks the tree and properly
	 * marks the node as being deleted.
	 */
	xas_lock(&xas);
	XA_BUG_ON(xa, xas_load(&xas) != xa_mk_value(1));
	node = xas.xa_node;
	XA_BUG_ON(xa, xa_entry_locked(xa, node, 0) != xa_mk_value(0));
	XA_BUG_ON(xa, xas_store(&xas, NULL) != xa_mk_value(1));
	XA_BUG_ON(xa, xa_load(xa, 1) != NULL);
	XA_BUG_ON(xa, xas.xa_node != XAS_BOUNDS);
	XA_BUG_ON(xa, xa_entry_locked(xa, node, 0) != XA_RETRY_ENTRY);
	XA_BUG_ON(xa, xas_load(&xas) != NULL);
	xas_unlock(&xas);
	XA_BUG_ON(xa, xa_load(xa, 0) != xa_mk_value(0));
	xa_erase_index(xa, 0);
	XA_BUG_ON(xa, !xa_empty(xa));
}

static noinline void check_cmpxchg(struct xarray *xa)
{
	void *FIVE = xa_mk_value(5);
	void *SIX = xa_mk_value(6);
	void *LOTS = xa_mk_value(12345678);

	XA_BUG_ON(xa, !xa_empty(xa));
	XA_BUG_ON(xa, xa_store_index(xa, 12345678, GFP_KERNEL) != NULL);
	XA_BUG_ON(xa, xa_insert(xa, 12345678, xa, GFP_KERNEL) != -EEXIST);
	XA_BUG_ON(xa, xa_cmpxchg(xa, 12345678, SIX, FIVE, GFP_KERNEL) != LOTS);
	XA_BUG_ON(xa, xa_cmpxchg(xa, 12345678, LOTS, FIVE, GFP_KERNEL) != LOTS);
	XA_BUG_ON(xa, xa_cmpxchg(xa, 12345678, FIVE, LOTS, GFP_KERNEL) != FIVE);
	XA_BUG_ON(xa, xa_cmpxchg(xa, 5, FIVE, NULL, GFP_KERNEL) != NULL);
	XA_BUG_ON(xa, xa_cmpxchg(xa, 5, NULL, FIVE, GFP_KERNEL) != NULL);
	xa_erase_index(xa, 12345678);
	xa_erase_index(xa, 5);
	XA_BUG_ON(xa, !xa_empty(xa));
}

static noinline void check_xas_erase(struct xarray *xa)
{
	XA_STATE(xas, xa, 0);
	void *entry;
	unsigned long i, j;

	for (i = 0; i < 200; i++) {
		for (j = i; j < 2 * i + 17; j++) {
			xas_set(&xas, j);
			do {
				xas_lock(&xas);
				xas_store(&xas, xa_mk_value(j));
				xas_unlock(&xas);
			} while (xas_nomem(&xas, GFP_KERNEL));
		}

		xas_set(&xas, ULONG_MAX);
		do {
			xas_lock(&xas);
			xas_store(&xas, xa_mk_value(0));
			xas_unlock(&xas);
		} while (xas_nomem(&xas, GFP_KERNEL));

		xas_lock(&xas);
		xas_store(&xas, NULL);

		xas_set(&xas, 0);
		j = i;
		xas_for_each(&xas, entry, ULONG_MAX) {
			XA_BUG_ON(xa, entry != xa_mk_value(j));
			xas_store(&xas, NULL);
			j++;
		}
		xas_unlock(&xas);
		XA_BUG_ON(xa, !xa_empty(xa));
	}
}

static noinline void check_multi_store(struct xarray *xa)
{
#ifdef CONFIG_XARRAY_MULTI
	unsigned long i, j, k;
	unsigned int max_order = (sizeof(long) == 4) ? 30 : 60;

	/* Loading from any position returns the same value */
	xa_store_order(xa, 0, 1, xa_mk_value(0), GFP_KERNEL);
	XA_BUG_ON(xa, xa_load(xa, 0) != xa_mk_value(0));
	XA_BUG_ON(xa, xa_load(xa, 1) != xa_mk_value(0));
	XA_BUG_ON(xa, xa_load(xa, 2) != NULL);
	rcu_read_lock();
	XA_BUG_ON(xa, xa_to_node(xa_head(xa))->count != 2);
	XA_BUG_ON(xa, xa_to_node(xa_head(xa))->nr_values != 2);
	rcu_read_unlock();

	/* Storing adjacent to the value does not alter the value */
	xa_store(xa, 3, xa, GFP_KERNEL);
	XA_BUG_ON(xa, xa_load(xa, 0) != xa_mk_value(0));
	XA_BUG_ON(xa, xa_load(xa, 1) != xa_mk_value(0));
	XA_BUG_ON(xa, xa_load(xa, 2) != NULL);
	rcu_read_lock();
	XA_BUG_ON(xa, xa_to_node(xa_head(xa))->count != 3);
	XA_BUG_ON(xa, xa_to_node(xa_head(xa))->nr_values != 2);
	rcu_read_unlock();

	/* Overwriting multiple indexes works */
	xa_store_order(xa, 0, 2, xa_mk_value(1), GFP_KERNEL);
	XA_BUG_ON(xa, xa_load(xa, 0) != xa_mk_value(1));
	XA_BUG_ON(xa, xa_load(xa, 1) != xa_mk_value(1));
	XA_BUG_ON(xa, xa_load(xa, 2) != xa_mk_value(1));
	XA_BUG_ON(xa, xa_load(xa, 3) != xa_mk_value(1));
	XA_BUG_ON(xa, xa_load(xa, 4) != NULL);
	rcu_read_lock();
	XA_BUG_ON(xa, xa_to_node(xa_head(xa))->count != 4);
	XA_BUG_ON(xa, xa_to_node(xa_head(xa))->nr_values != 4);
	rcu_read_unlock();

	/* We can erase multiple values with a single store */
	xa_store_order(xa, 0, 63, NULL, GFP_KERNEL);
	XA_BUG_ON(xa, !xa_empty(xa));

	/* Even when the first slot is empty but the others aren't */
	xa_store_index(xa, 1, GFP_KERNEL);
	xa_store_index(xa, 2, GFP_KERNEL);
	xa_store_order(xa, 0, 2, NULL, GFP_KERNEL);
	XA_BUG_ON(xa, !xa_empty(xa));

	for (i = 0; i < max_order; i++) {
		for (j = 0; j < max_order; j++) {
			xa_store_order(xa, 0, i, xa_mk_value(i), GFP_KERNEL);
			xa_store_order(xa, 0, j, xa_mk_value(j), GFP_KERNEL);

			for (k = 0; k < max_order; k++) {
				void *entry = xa_load(xa, (1UL << k) - 1);
				if ((i < k) && (j < k))
					XA_BUG_ON(xa, entry != NULL);
				else
					XA_BUG_ON(xa, entry != xa_mk_value(j));
			}

			xa_erase(xa, 0);
			XA_BUG_ON(xa, !xa_empty(xa));
		}
	}
#endif
}

static noinline void check_multi_find(struct xarray *xa)
{
#ifdef CONFIG_XARRAY_MULTI
	unsigned long index;

	xa_store_order(xa, 12, 2, xa_mk_value(12), GFP_KERNEL);
	XA_BUG_ON(xa, xa_store_index(xa, 16, GFP_KERNEL) != NULL);

	index = 0;
	XA_BUG_ON(xa, xa_find(xa, &index, ULONG_MAX, XA_PRESENT) !=
			xa_mk_value(12));
	XA_BUG_ON(xa, index != 12);
	index = 13;
	XA_BUG_ON(xa, xa_find(xa, &index, ULONG_MAX, XA_PRESENT) !=
			xa_mk_value(12));
	XA_BUG_ON(xa, (index < 12) || (index >= 16));
	XA_BUG_ON(xa, xa_find_after(xa, &index, ULONG_MAX, XA_PRESENT) !=
			xa_mk_value(16));
	XA_BUG_ON(xa, index != 16);

	xa_erase_index(xa, 12);
	xa_erase_index(xa, 16);
	XA_BUG_ON(xa, !xa_empty(xa));
#endif
}

static noinline void check_multi_find_2(struct xarray *xa)
{
	unsigned int max_order = IS_ENABLED(CONFIG_XARRAY_MULTI) ? 10 : 1;
	unsigned int i, j;
	void *entry;

	for (i = 0; i < max_order; i++) {
		unsigned long index = 1UL << i;
		for (j = 0; j < index; j++) {
			XA_STATE(xas, xa, j + index);
			xa_store_index(xa, index - 1, GFP_KERNEL);
			xa_store_order(xa, index, i, xa_mk_value(index),
					GFP_KERNEL);
			rcu_read_lock();
			xas_for_each(&xas, entry, ULONG_MAX) {
				xa_erase_index(xa, index);
			}
			rcu_read_unlock();
			xa_erase_index(xa, index - 1);
			XA_BUG_ON(xa, !xa_empty(xa));
		}
	}
}

static noinline void check_find(struct xarray *xa)
{
	unsigned long i, j, k;

	XA_BUG_ON(xa, !xa_empty(xa));

	/*
	 * Check xa_find with all pairs between 0 and 99 inclusive,
	 * starting at every index between 0 and 99
	 */
	for (i = 0; i < 100; i++) {
		XA_BUG_ON(xa, xa_store_index(xa, i, GFP_KERNEL) != NULL);
		xa_set_mark(xa, i, XA_MARK_0);
		for (j = 0; j < i; j++) {
			XA_BUG_ON(xa, xa_store_index(xa, j, GFP_KERNEL) !=
					NULL);
			xa_set_mark(xa, j, XA_MARK_0);
			for (k = 0; k < 100; k++) {
				unsigned long index = k;
				void *entry = xa_find(xa, &index, ULONG_MAX,
								XA_PRESENT);
				if (k <= j)
					XA_BUG_ON(xa, index != j);
				else if (k <= i)
					XA_BUG_ON(xa, index != i);
				else
					XA_BUG_ON(xa, entry != NULL);

				index = k;
				entry = xa_find(xa, &index, ULONG_MAX,
								XA_MARK_0);
				if (k <= j)
					XA_BUG_ON(xa, index != j);
				else if (k <= i)
					XA_BUG_ON(xa, index != i);
				else
					XA_BUG_ON(xa, entry != NULL);
			}
			xa_erase_index(xa, j);
			XA_BUG_ON(xa, xa_get_mark(xa, j, XA_MARK_0));
			XA_BUG_ON(xa, !xa_get_mark(xa, i, XA_MARK_0));
		}
		xa_erase_index(xa, i);
		XA_BUG_ON(xa, xa_get_mark(xa, i, XA_MARK_0));
	}
	XA_BUG_ON(xa, !xa_empty(xa));
	check_multi_find(xa);
	check_multi_find_2(xa);
}

static noinline void check_destroy(struct xarray *xa)
{
	unsigned long index;

	XA_BUG_ON(xa, !xa_empty(xa));

	/* Destroying an empty array is a no-op */
	xa_destroy(xa);
	XA_BUG_ON(xa, !xa_empty(xa));

	/* Destroying an array with a single entry */
	for (index = 0; index < 1000; index++) {
		xa_store_index(xa, index, GFP_KERNEL);
		XA_BUG_ON(xa, xa_empty(xa));
		xa_destroy(xa);
		XA_BUG_ON(xa, !xa_empty(xa));
	}

	/* Destroying an array with a single entry at ULONG_MAX */
	xa_store(xa, ULONG_MAX, xa, GFP_KERNEL);
	XA_BUG_ON(xa, xa_empty(xa));
	xa_destroy(xa);
	XA_BUG_ON(xa, !xa_empty(xa));

#ifdef CONFIG_XARRAY_MULTI
	/* Destroying an array with a multi-index entry */
	xa_store_order(xa, 1 << 11, 11, xa, GFP_KERNEL);
	XA_BUG_ON(xa, xa_empty(xa));
	xa_destroy(xa);
	XA_BUG_ON(xa, !xa_empty(xa));
#endif
}

static DEFINE_XARRAY(array);

static int xarray_checks(void)
{
	check_xa_err(&array);
	check_xas_retry(&array);
	check_xa_load(&array);
	check_xa_mark(&array);
	check_xa_shrink(&array);
	check_xas_erase(&array);
	check_cmpxchg(&array);
	check_multi_store(&array);
	check_find(&array);
	check_destroy(&array);

	printk("XArray: %u of %u tests passed\n", tests_passed, tests_run);
	return (tests_run == tests_passed) ? 0 : -EINVAL;
}

static void xarray_exit(void)
{
}

module_init(xarray_checks);
module_exit(xarray_exit);
MODULE_AUTHOR("Matthew Wilcox <willy@infradead.org>");
MODULE_LICENSE("GPL");
