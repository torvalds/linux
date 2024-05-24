// SPDX-License-Identifier: GPL-2.0-only
#define pr_fmt(fmt) "min_heap_test: " fmt

/*
 * Test cases for the min max heap.
 */

#include <linux/log2.h>
#include <linux/min_heap.h>
#include <linux/module.h>
#include <linux/printk.h>
#include <linux/random.h>

DEFINE_MIN_HEAP(int, min_heap_test);

static __init bool less_than(const void *lhs, const void *rhs, void __always_unused *args)
{
	return *(int *)lhs < *(int *)rhs;
}

static __init bool greater_than(const void *lhs, const void *rhs, void __always_unused *args)
{
	return *(int *)lhs > *(int *)rhs;
}

static __init void swap_ints(void *lhs, void *rhs, void __always_unused *args)
{
	int temp = *(int *)lhs;

	*(int *)lhs = *(int *)rhs;
	*(int *)rhs = temp;
}

static __init int pop_verify_heap(bool min_heap,
				struct min_heap_test *heap,
				const struct min_heap_callbacks *funcs)
{
	int *values = heap->data;
	int err = 0;
	int last;

	last = values[0];
	min_heap_pop(heap, funcs, NULL);
	while (heap->nr > 0) {
		if (min_heap) {
			if (last > values[0]) {
				pr_err("error: expected %d <= %d\n", last,
					values[0]);
				err++;
			}
		} else {
			if (last < values[0]) {
				pr_err("error: expected %d >= %d\n", last,
					values[0]);
				err++;
			}
		}
		last = values[0];
		min_heap_pop(heap, funcs, NULL);
	}
	return err;
}

static __init int test_heapify_all(bool min_heap)
{
	int values[] = { 3, 1, 2, 4, 0x8000000, 0x7FFFFFF, 0,
			 -3, -1, -2, -4, 0x8000000, 0x7FFFFFF };
	struct min_heap_test heap = {
		.data = values,
		.nr = ARRAY_SIZE(values),
		.size =  ARRAY_SIZE(values),
	};
	struct min_heap_callbacks funcs = {
		.less = min_heap ? less_than : greater_than,
		.swp = swap_ints,
	};
	int i, err;

	/* Test with known set of values. */
	min_heapify_all(&heap, &funcs, NULL);
	err = pop_verify_heap(min_heap, &heap, &funcs);


	/* Test with randomly generated values. */
	heap.nr = ARRAY_SIZE(values);
	for (i = 0; i < heap.nr; i++)
		values[i] = get_random_u32();

	min_heapify_all(&heap, &funcs, NULL);
	err += pop_verify_heap(min_heap, &heap, &funcs);

	return err;
}

static __init int test_heap_push(bool min_heap)
{
	const int data[] = { 3, 1, 2, 4, 0x80000000, 0x7FFFFFFF, 0,
			     -3, -1, -2, -4, 0x80000000, 0x7FFFFFFF };
	int values[ARRAY_SIZE(data)];
	struct min_heap_test heap = {
		.data = values,
		.nr = 0,
		.size =  ARRAY_SIZE(values),
	};
	struct min_heap_callbacks funcs = {
		.less = min_heap ? less_than : greater_than,
		.swp = swap_ints,
	};
	int i, temp, err;

	/* Test with known set of values copied from data. */
	for (i = 0; i < ARRAY_SIZE(data); i++)
		min_heap_push(&heap, &data[i], &funcs, NULL);

	err = pop_verify_heap(min_heap, &heap, &funcs);

	/* Test with randomly generated values. */
	while (heap.nr < heap.size) {
		temp = get_random_u32();
		min_heap_push(&heap, &temp, &funcs, NULL);
	}
	err += pop_verify_heap(min_heap, &heap, &funcs);

	return err;
}

static __init int test_heap_pop_push(bool min_heap)
{
	const int data[] = { 3, 1, 2, 4, 0x80000000, 0x7FFFFFFF, 0,
			     -3, -1, -2, -4, 0x80000000, 0x7FFFFFFF };
	int values[ARRAY_SIZE(data)];
	struct min_heap_test heap = {
		.data = values,
		.nr = 0,
		.size =  ARRAY_SIZE(values),
	};
	struct min_heap_callbacks funcs = {
		.less = min_heap ? less_than : greater_than,
		.swp = swap_ints,
	};
	int i, temp, err;

	/* Fill values with data to pop and replace. */
	temp = min_heap ? 0x80000000 : 0x7FFFFFFF;
	for (i = 0; i < ARRAY_SIZE(data); i++)
		min_heap_push(&heap, &temp, &funcs, NULL);

	/* Test with known set of values copied from data. */
	for (i = 0; i < ARRAY_SIZE(data); i++)
		min_heap_pop_push(&heap, &data[i], &funcs, NULL);

	err = pop_verify_heap(min_heap, &heap, &funcs);

	heap.nr = 0;
	for (i = 0; i < ARRAY_SIZE(data); i++)
		min_heap_push(&heap, &temp, &funcs, NULL);

	/* Test with randomly generated values. */
	for (i = 0; i < ARRAY_SIZE(data); i++) {
		temp = get_random_u32();
		min_heap_pop_push(&heap, &temp, &funcs, NULL);
	}
	err += pop_verify_heap(min_heap, &heap, &funcs);

	return err;
}

static int __init test_min_heap_init(void)
{
	int err = 0;

	err += test_heapify_all(true);
	err += test_heapify_all(false);
	err += test_heap_push(true);
	err += test_heap_push(false);
	err += test_heap_pop_push(true);
	err += test_heap_pop_push(false);
	if (err) {
		pr_err("test failed with %d errors\n", err);
		return -EINVAL;
	}
	pr_info("test passed\n");
	return 0;
}
module_init(test_min_heap_init);

static void __exit test_min_heap_exit(void)
{
	/* do nothing */
}
module_exit(test_min_heap_exit);

MODULE_LICENSE("GPL");
