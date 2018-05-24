// SPDX-License-Identifier: GPL-2.0
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/module.h>

typedef void(*test_ubsan_fp)(void);

static void test_ubsan_add_overflow(void)
{
	volatile int val = INT_MAX;

	val += 2;
}

static void test_ubsan_sub_overflow(void)
{
	volatile int val = INT_MIN;
	volatile int val2 = 2;

	val -= val2;
}

static void test_ubsan_mul_overflow(void)
{
	volatile int val = INT_MAX / 2;

	val *= 3;
}

static void test_ubsan_negate_overflow(void)
{
	volatile int val = INT_MIN;

	val = -val;
}

static void test_ubsan_divrem_overflow(void)
{
	volatile int val = 16;
	volatile int val2 = 0;

	val /= val2;
}

static void test_ubsan_vla_bound_not_positive(void)
{
	volatile int size = -1;
	char buf[size];

	(void)buf;
}

static void test_ubsan_shift_out_of_bounds(void)
{
	volatile int val = -1;
	int val2 = 10;

	val2 <<= val;
}

static void test_ubsan_out_of_bounds(void)
{
	volatile int i = 4, j = 5;
	volatile int arr[i];

	arr[j] = i;
}

static void test_ubsan_load_invalid_value(void)
{
	volatile char *dst, *src;
	bool val, val2, *ptr;
	char c = 4;

	dst = (char *)&val;
	src = &c;
	*dst = *src;

	ptr = &val2;
	val2 = val;
}

static void test_ubsan_null_ptr_deref(void)
{
	volatile int *ptr = NULL;
	int val;

	val = *ptr;
}

static void test_ubsan_misaligned_access(void)
{
	volatile char arr[5] __aligned(4) = {1, 2, 3, 4, 5};
	volatile int *ptr, val = 6;

	ptr = (int *)(arr + 1);
	*ptr = val;
}

static void test_ubsan_object_size_mismatch(void)
{
	/* "((aligned(8)))" helps this not into be misaligned for ptr-access. */
	volatile int val __aligned(8) = 4;
	volatile long long *ptr, val2;

	ptr = (long long *)&val;
	val2 = *ptr;
}

static const test_ubsan_fp test_ubsan_array[] = {
	test_ubsan_add_overflow,
	test_ubsan_sub_overflow,
	test_ubsan_mul_overflow,
	test_ubsan_negate_overflow,
	test_ubsan_divrem_overflow,
	test_ubsan_vla_bound_not_positive,
	test_ubsan_shift_out_of_bounds,
	test_ubsan_out_of_bounds,
	test_ubsan_load_invalid_value,
	//test_ubsan_null_ptr_deref, /* exclude it because there is a crash */
	test_ubsan_misaligned_access,
	test_ubsan_object_size_mismatch,
};

static int __init test_ubsan_init(void)
{
	unsigned int i;

	for (i = 0; i < ARRAY_SIZE(test_ubsan_array); i++)
		test_ubsan_array[i]();

	(void)test_ubsan_null_ptr_deref; /* to avoid unsed-function warning */
	return 0;
}
module_init(test_ubsan_init);

static void __exit test_ubsan_exit(void)
{
	/* do nothing */
}
module_exit(test_ubsan_exit);

MODULE_AUTHOR("Jinbum Park <jinb.park7@gmail.com>");
MODULE_LICENSE("GPL v2");
