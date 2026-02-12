// SPDX-License-Identifier: GPL-2.0-only
/*
 * Test cases for the min max heap.
 */

#include <kunit/test.h>
#include <linux/min_heap.h>
#include <linux/module.h>
#include <linux/random.h>

struct min_heap_test_case {
	const char *str;
	bool min_heap;
};

static struct min_heap_test_case min_heap_cases[] = {
	{
		.str = "min",
		.min_heap = true,
	},
	{
		.str = "max",
		.min_heap = false,
	},
};

KUNIT_ARRAY_PARAM_DESC(min_heap, min_heap_cases, str);

DEFINE_MIN_HEAP(int, min_heap_test);

static bool less_than(const void *lhs, const void *rhs, void __always_unused *args)
{
	return *(int *)lhs < *(int *)rhs;
}

static bool greater_than(const void *lhs, const void *rhs, void __always_unused *args)
{
	return *(int *)lhs > *(int *)rhs;
}

static void pop_verify_heap(struct kunit *test,
			    bool min_heap,
			    struct min_heap_test *heap,
			    const struct min_heap_callbacks *funcs)
{
	int *values = heap->data;
	int last;

	last = values[0];
	min_heap_pop_inline(heap, funcs, NULL);
	while (heap->nr > 0) {
		if (min_heap)
			KUNIT_EXPECT_LE(test, last, values[0]);
		else
			KUNIT_EXPECT_GE(test, last, values[0]);
		last = values[0];
		min_heap_pop_inline(heap, funcs, NULL);
	}
}

static void test_heapify_all(struct kunit *test)
{
	const struct min_heap_test_case *params = test->param_value;
	int values[] = { 3, 1, 2, 4, 0x8000000, 0x7FFFFFF, 0,
			 -3, -1, -2, -4, 0x8000000, 0x7FFFFFF };
	struct min_heap_test heap = {
		.data = values,
		.nr = ARRAY_SIZE(values),
		.size =  ARRAY_SIZE(values),
	};
	struct min_heap_callbacks funcs = {
		.less = params->min_heap ? less_than : greater_than,
		.swp = NULL,
	};
	int i;

	/* Test with known set of values. */
	min_heapify_all_inline(&heap, &funcs, NULL);
	pop_verify_heap(test, params->min_heap, &heap, &funcs);

	/* Test with randomly generated values. */
	heap.nr = ARRAY_SIZE(values);
	for (i = 0; i < heap.nr; i++)
		values[i] = get_random_u32();

	min_heapify_all_inline(&heap, &funcs, NULL);
	pop_verify_heap(test, params->min_heap, &heap, &funcs);
}

static void test_heap_push(struct kunit *test)
{
	const struct min_heap_test_case *params = test->param_value;
	const int data[] = { 3, 1, 2, 4, 0x80000000, 0x7FFFFFFF, 0,
			     -3, -1, -2, -4, 0x80000000, 0x7FFFFFFF };
	int values[ARRAY_SIZE(data)];
	struct min_heap_test heap = {
		.data = values,
		.nr = 0,
		.size =  ARRAY_SIZE(values),
	};
	struct min_heap_callbacks funcs = {
		.less = params->min_heap ? less_than : greater_than,
		.swp = NULL,
	};
	int i, temp;

	/* Test with known set of values copied from data. */
	for (i = 0; i < ARRAY_SIZE(data); i++)
		min_heap_push_inline(&heap, &data[i], &funcs, NULL);

	pop_verify_heap(test, params->min_heap, &heap, &funcs);

	/* Test with randomly generated values. */
	while (heap.nr < heap.size) {
		temp = get_random_u32();
		min_heap_push_inline(&heap, &temp, &funcs, NULL);
	}
	pop_verify_heap(test, params->min_heap, &heap, &funcs);
}

static void test_heap_pop_push(struct kunit *test)
{
	const struct min_heap_test_case *params = test->param_value;
	const int data[] = { 3, 1, 2, 4, 0x80000000, 0x7FFFFFFF, 0,
			     -3, -1, -2, -4, 0x80000000, 0x7FFFFFFF };
	int values[ARRAY_SIZE(data)];
	struct min_heap_test heap = {
		.data = values,
		.nr = 0,
		.size =  ARRAY_SIZE(values),
	};
	struct min_heap_callbacks funcs = {
		.less = params->min_heap ? less_than : greater_than,
		.swp = NULL,
	};
	int i, temp;

	/* Fill values with data to pop and replace. */
	temp = params->min_heap ? 0x80000000 : 0x7FFFFFFF;
	for (i = 0; i < ARRAY_SIZE(data); i++)
		min_heap_push_inline(&heap, &temp, &funcs, NULL);

	/* Test with known set of values copied from data. */
	for (i = 0; i < ARRAY_SIZE(data); i++)
		min_heap_pop_push_inline(&heap, &data[i], &funcs, NULL);

	pop_verify_heap(test, params->min_heap, &heap, &funcs);

	heap.nr = 0;
	for (i = 0; i < ARRAY_SIZE(data); i++)
		min_heap_push_inline(&heap, &temp, &funcs, NULL);

	/* Test with randomly generated values. */
	for (i = 0; i < ARRAY_SIZE(data); i++) {
		temp = get_random_u32();
		min_heap_pop_push_inline(&heap, &temp, &funcs, NULL);
	}
	pop_verify_heap(test, params->min_heap, &heap, &funcs);
}

static void test_heap_del(struct kunit *test)
{
	const struct min_heap_test_case *params = test->param_value;
	int values[] = { 3, 1, 2, 4, 0x8000000, 0x7FFFFFF, 0,
			 -3, -1, -2, -4, 0x8000000, 0x7FFFFFF };
	struct min_heap_test heap;

	min_heap_init_inline(&heap, values, ARRAY_SIZE(values));
	heap.nr = ARRAY_SIZE(values);
	struct min_heap_callbacks funcs = {
		.less = params->min_heap ? less_than : greater_than,
		.swp = NULL,
	};
	int i;

	/* Test with known set of values. */
	min_heapify_all_inline(&heap, &funcs, NULL);
	for (i = 0; i < ARRAY_SIZE(values) / 2; i++)
		min_heap_del_inline(&heap, get_random_u32() % heap.nr, &funcs, NULL);
	pop_verify_heap(test, params->min_heap, &heap, &funcs);

	/* Test with randomly generated values. */
	heap.nr = ARRAY_SIZE(values);
	for (i = 0; i < heap.nr; i++)
		values[i] = get_random_u32();
	min_heapify_all_inline(&heap, &funcs, NULL);

	for (i = 0; i < ARRAY_SIZE(values) / 2; i++)
		min_heap_del_inline(&heap, get_random_u32() % heap.nr, &funcs, NULL);
	pop_verify_heap(test, params->min_heap, &heap, &funcs);
}

static struct kunit_case min_heap_test_cases[] = {
	KUNIT_CASE_PARAM(test_heapify_all, min_heap_gen_params),
	KUNIT_CASE_PARAM(test_heap_push, min_heap_gen_params),
	KUNIT_CASE_PARAM(test_heap_pop_push, min_heap_gen_params),
	KUNIT_CASE_PARAM(test_heap_del, min_heap_gen_params),
	{},
};

static struct kunit_suite min_heap_test_suite = {
	.name = "min_heap",
	.test_cases = min_heap_test_cases,
};

kunit_test_suite(min_heap_test_suite);

MODULE_DESCRIPTION("Test cases for the min max heap");
MODULE_LICENSE("GPL");
