// SPDX-License-Identifier: GPL-2.0-only
/*
 * Test for find_*_bit functions.
 *
 * Copyright (c) 2017 Cavium.
 */

/*
 * find_bit functions are widely used in kernel, so the successful boot
 * is good enough test for correctness.
 *
 * This test is focused on performance of traversing bitmaps. Two typical
 * scenarios are reproduced:
 * - randomly filled bitmap with approximately equal number of set and
 *   cleared bits;
 * - sparse bitmap with few set bits at random positions.
 */

#include <linux/bitops.h>
#include <linux/kernel.h>
#include <linux/list.h>
#include <linux/module.h>
#include <linux/printk.h>
#include <linux/random.h>

#define BITMAP_LEN	(4096UL * 8 * 10)
#define SPARSE		500

static DECLARE_BITMAP(bitmap, BITMAP_LEN) __initdata;
static DECLARE_BITMAP(bitmap2, BITMAP_LEN) __initdata;

/*
 * This is Schlemiel the Painter's algorithm. It should be called after
 * all other tests for the same bitmap because it sets all bits of bitmap to 1.
 */
static int __init test_find_first_bit(void *bitmap, unsigned long len)
{
	unsigned long i, cnt;
	ktime_t time;

	time = ktime_get();
	for (cnt = i = 0; i < len; cnt++) {
		i = find_first_bit(bitmap, len);
		__clear_bit(i, bitmap);
	}
	time = ktime_get() - time;
	pr_err("find_first_bit:     %18llu ns, %6ld iterations\n", time, cnt);

	return 0;
}

static int __init test_find_first_and_bit(void *bitmap, const void *bitmap2, unsigned long len)
{
	static DECLARE_BITMAP(cp, BITMAP_LEN) __initdata;
	unsigned long i, cnt;
	ktime_t time;

	bitmap_copy(cp, bitmap, BITMAP_LEN);

	time = ktime_get();
	for (cnt = i = 0; i < len; cnt++) {
		i = find_first_and_bit(cp, bitmap2, len);
		__clear_bit(i, cp);
	}
	time = ktime_get() - time;
	pr_err("find_first_and_bit: %18llu ns, %6ld iterations\n", time, cnt);

	return 0;
}

static int __init test_find_next_bit(const void *bitmap, unsigned long len)
{
	unsigned long i, cnt;
	ktime_t time;

	time = ktime_get();
	for (cnt = i = 0; i < BITMAP_LEN; cnt++)
		i = find_next_bit(bitmap, BITMAP_LEN, i) + 1;
	time = ktime_get() - time;
	pr_err("find_next_bit:      %18llu ns, %6ld iterations\n", time, cnt);

	return 0;
}

static int __init test_find_next_zero_bit(const void *bitmap, unsigned long len)
{
	unsigned long i, cnt;
	ktime_t time;

	time = ktime_get();
	for (cnt = i = 0; i < BITMAP_LEN; cnt++)
		i = find_next_zero_bit(bitmap, len, i) + 1;
	time = ktime_get() - time;
	pr_err("find_next_zero_bit: %18llu ns, %6ld iterations\n", time, cnt);

	return 0;
}

static int __init test_find_last_bit(const void *bitmap, unsigned long len)
{
	unsigned long l, cnt = 0;
	ktime_t time;

	time = ktime_get();
	do {
		cnt++;
		l = find_last_bit(bitmap, len);
		if (l >= len)
			break;
		len = l;
	} while (len);
	time = ktime_get() - time;
	pr_err("find_last_bit:      %18llu ns, %6ld iterations\n", time, cnt);

	return 0;
}

static int __init test_find_next_and_bit(const void *bitmap,
		const void *bitmap2, unsigned long len)
{
	unsigned long i, cnt;
	ktime_t time;

	time = ktime_get();
	for (cnt = i = 0; i < BITMAP_LEN; cnt++)
		i = find_next_and_bit(bitmap, bitmap2, BITMAP_LEN, i + 1);
	time = ktime_get() - time;
	pr_err("find_next_and_bit:  %18llu ns, %6ld iterations\n", time, cnt);

	return 0;
}

static int __init find_bit_test(void)
{
	unsigned long nbits = BITMAP_LEN / SPARSE;

	pr_err("\nStart testing find_bit() with random-filled bitmap\n");

	get_random_bytes(bitmap, sizeof(bitmap));
	get_random_bytes(bitmap2, sizeof(bitmap2));

	test_find_next_bit(bitmap, BITMAP_LEN);
	test_find_next_zero_bit(bitmap, BITMAP_LEN);
	test_find_last_bit(bitmap, BITMAP_LEN);

	/*
	 * test_find_first_bit() may take some time, so
	 * traverse only part of bitmap to avoid soft lockup.
	 */
	test_find_first_bit(bitmap, BITMAP_LEN / 10);
	test_find_first_and_bit(bitmap, bitmap2, BITMAP_LEN / 2);
	test_find_next_and_bit(bitmap, bitmap2, BITMAP_LEN);

	pr_err("\nStart testing find_bit() with sparse bitmap\n");

	bitmap_zero(bitmap, BITMAP_LEN);
	bitmap_zero(bitmap2, BITMAP_LEN);

	while (nbits--) {
		__set_bit(prandom_u32() % BITMAP_LEN, bitmap);
		__set_bit(prandom_u32() % BITMAP_LEN, bitmap2);
	}

	test_find_next_bit(bitmap, BITMAP_LEN);
	test_find_next_zero_bit(bitmap, BITMAP_LEN);
	test_find_last_bit(bitmap, BITMAP_LEN);
	test_find_first_bit(bitmap, BITMAP_LEN);
	test_find_first_and_bit(bitmap, bitmap2, BITMAP_LEN);
	test_find_next_and_bit(bitmap, bitmap2, BITMAP_LEN);

	/*
	 * Everything is OK. Return error just to let user run benchmark
	 * again without annoying rmmod.
	 */
	return -EINVAL;
}
module_init(find_bit_test);

MODULE_LICENSE("GPL");
