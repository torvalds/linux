// SPDX-License-Identifier: GPL-2.0-only

#include <kunit/test.h>

#include <linux/sort.h>
#include <linux/slab.h>
#include <linux/module.h>

/* a simple boot-time regression test */

#define TEST_LEN 1000

static int cmpint(const void *a, const void *b)
{
	return *(int *)a - *(int *)b;
}

static void test_sort(struct kunit *test)
{
	int *a, i, r = 1;

	a = kunit_kmalloc_array(test, TEST_LEN, sizeof(*a), GFP_KERNEL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, a);

	for (i = 0; i < TEST_LEN; i++) {
		r = (r * 725861) % 6599;
		a[i] = r;
	}

	sort(a, TEST_LEN, sizeof(*a), cmpint, NULL);

	for (i = 0; i < TEST_LEN - 1; i++)
		KUNIT_ASSERT_LE(test, a[i], a[i + 1]);

	r = 48;

	for (i = 0; i < TEST_LEN - 1; i++) {
		r = (r * 725861) % 6599;
		a[i] = r;
	}

	sort(a, TEST_LEN - 1, sizeof(*a), cmpint, NULL);

	for (i = 0; i < TEST_LEN - 2; i++)
		KUNIT_ASSERT_LE(test, a[i], a[i + 1]);
}

static struct kunit_case sort_test_cases[] = {
	KUNIT_CASE(test_sort),
	{}
};

static struct kunit_suite sort_test_suite = {
	.name = "lib_sort",
	.test_cases = sort_test_cases,
};

kunit_test_suites(&sort_test_suite);

MODULE_DESCRIPTION("sort() KUnit test suite");
MODULE_LICENSE("GPL");
