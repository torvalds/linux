// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2024 BayLibre SAS
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/init.h>
#include <linux/module.h>
#include <linux/printk.h>
#include <linux/math64.h>

typedef struct { u64 a; u64 b; u64 d; u64 result; uint round_up;} test_params;

static test_params test_values[] = {
/* this contains many edge values followed by a couple random values */
{                0xb,                0x7,                0x3,               0x19, 1 },
{         0xffff0000,         0xffff0000,                0xf, 0x1110eeef00000000, 0 },
{         0xffffffff,         0xffffffff,                0x1, 0xfffffffe00000001, 0 },
{         0xffffffff,         0xffffffff,                0x2, 0x7fffffff00000000, 1 },
{        0x1ffffffff,         0xffffffff,                0x2, 0xfffffffe80000000, 1 },
{        0x1ffffffff,         0xffffffff,                0x3, 0xaaaaaaa9aaaaaaab, 0 },
{        0x1ffffffff,        0x1ffffffff,                0x4, 0xffffffff00000000, 1 },
{ 0xffff000000000000, 0xffff000000000000, 0xffff000000000001, 0xfffeffffffffffff, 1 },
{ 0x3333333333333333, 0x3333333333333333, 0x5555555555555555, 0x1eb851eb851eb851, 1 },
{ 0x7fffffffffffffff,                0x2,                0x3, 0x5555555555555554, 1 },
{ 0xffffffffffffffff,                0x2, 0x8000000000000000,                0x3, 1 },
{ 0xffffffffffffffff,                0x2, 0xc000000000000000,                0x2, 1 },
{ 0xffffffffffffffff, 0x4000000000000004, 0x8000000000000000, 0x8000000000000007, 1 },
{ 0xffffffffffffffff, 0x4000000000000001, 0x8000000000000000, 0x8000000000000001, 1 },
{ 0xffffffffffffffff, 0x8000000000000001, 0xffffffffffffffff, 0x8000000000000001, 0 },
{ 0xfffffffffffffffe, 0x8000000000000001, 0xffffffffffffffff, 0x8000000000000000, 1 },
{ 0xffffffffffffffff, 0x8000000000000001, 0xfffffffffffffffe, 0x8000000000000001, 1 },
{ 0xffffffffffffffff, 0x8000000000000001, 0xfffffffffffffffd, 0x8000000000000002, 1 },
{ 0x7fffffffffffffff, 0xffffffffffffffff, 0xc000000000000000, 0xaaaaaaaaaaaaaaa8, 1 },
{ 0xffffffffffffffff, 0x7fffffffffffffff, 0xa000000000000000, 0xccccccccccccccca, 1 },
{ 0xffffffffffffffff, 0x7fffffffffffffff, 0x9000000000000000, 0xe38e38e38e38e38b, 1 },
{ 0x7fffffffffffffff, 0x7fffffffffffffff, 0x5000000000000000, 0xccccccccccccccc9, 1 },
{ 0xffffffffffffffff, 0xfffffffffffffffe, 0xffffffffffffffff, 0xfffffffffffffffe, 0 },
{ 0xe6102d256d7ea3ae, 0x70a77d0be4c31201, 0xd63ec35ab3220357, 0x78f8bf8cc86c6e18, 1 },
{ 0xf53bae05cb86c6e1, 0x3847b32d2f8d32e0, 0xcfd4f55a647f403c, 0x42687f79d8998d35, 1 },
{ 0x9951c5498f941092, 0x1f8c8bfdf287a251, 0xa3c8dc5f81ea3fe2, 0x1d887cb25900091f, 1 },
{ 0x374fee9daa1bb2bb, 0x0d0bfbff7b8ae3ef, 0xc169337bd42d5179, 0x03bb2dbaffcbb961, 1 },
{ 0xeac0d03ac10eeaf0, 0x89be05dfa162ed9b, 0x92bb1679a41f0e4b, 0xdc5f5cc9e270d216, 1 },
};

/*
 * The above table can be verified with the following shell script:

#!/bin/sh
sed -ne 's/^{ \+\(.*\), \+\(.*\), \+\(.*\), \+\(.*\), \+\(.*\) },$/\1 \2 \3 \4 \5/p' \
    lib/math/test_mul_u64_u64_div_u64.c |
while read a b d r e; do
  expected=$( printf "obase=16; ibase=16; %X * %X / %X\n" $a $b $d | bc )
  given=$( printf "%X\n" $r )
  if [ "$expected" = "$given" ]; then
    echo "$a * $b  / $d = $r OK"
  else
    echo "$a * $b  / $d = $r is wrong" >&2
    echo "should be equivalent to 0x$expected" >&2
    exit 1
  fi
  expected=$( printf "obase=16; ibase=16; (%X * %X + %X) / %X\n" $a $b $((d-1)) $d | bc )
  given=$( printf "%X\n" $((r + e)) )
  if [ "$expected" = "$given" ]; then
    echo "$a * $b +/ $d = $(printf '%#x' $((r + e))) OK"
  else
    echo "$a * $b +/ $d = $(printf '%#x' $((r + e))) is wrong" >&2
    echo "should be equivalent to 0x$expected" >&2
    exit 1
  fi
done

 */

static u64 test_mul_u64_add_u64_div_u64(u64 a, u64 b, u64 c, u64 d);
#if __LONG_WIDTH__ >= 64
#define TEST_32BIT_DIV
static u64 test_mul_u64_add_u64_div_u64_32bit(u64 a, u64 b, u64 c, u64 d);
#endif

static int __init test_run(unsigned int fn_no, const char *fn_name)
{
	u64 start_time;
	int errors = 0;
	int tests = 0;
	int i;

	start_time = ktime_get_ns();

	for (i = 0; i < ARRAY_SIZE(test_values); i++) {
		u64 a = test_values[i].a;
		u64 b = test_values[i].b;
		u64 d = test_values[i].d;
		u64 expected_result = test_values[i].result;
		u64 result, result_up;

		switch (fn_no) {
		default:
			result = mul_u64_u64_div_u64(a, b, d);
			result_up = mul_u64_u64_div_u64_roundup(a, b, d);
			break;
		case 1:
			result = test_mul_u64_add_u64_div_u64(a, b, 0, d);
			result_up = test_mul_u64_add_u64_div_u64(a, b, d - 1, d);
			break;
#ifdef TEST_32BIT_DIV
		case 2:
			result = test_mul_u64_add_u64_div_u64_32bit(a, b, 0, d);
			result_up = test_mul_u64_add_u64_div_u64_32bit(a, b, d - 1, d);
			break;
#endif
		}

		tests += 2;

		if (result != expected_result) {
			pr_err("ERROR: 0x%016llx * 0x%016llx / 0x%016llx\n", a, b, d);
			pr_err("ERROR: expected result: %016llx\n", expected_result);
			pr_err("ERROR: obtained result: %016llx\n", result);
			errors++;
		}
		expected_result += test_values[i].round_up;
		if (result_up != expected_result) {
			pr_err("ERROR: 0x%016llx * 0x%016llx +/ 0x%016llx\n", a, b, d);
			pr_err("ERROR: expected result: %016llx\n", expected_result);
			pr_err("ERROR: obtained result: %016llx\n", result_up);
			errors++;
		}
	}

	pr_info("Completed %s() test, %d tests, %d errors, %llu ns\n",
		fn_name, tests, errors, ktime_get_ns() - start_time);
	return errors;
}

static int __init test_init(void)
{
	pr_info("Starting mul_u64_u64_div_u64() test\n");
	if (test_run(0, "mul_u64_u64_div_u64"))
		return -EINVAL;
	if (test_run(1, "test_mul_u64_u64_div_u64"))
		return -EINVAL;
#ifdef TEST_32BIT_DIV
	if (test_run(2, "test_mul_u64_u64_div_u64_32bit"))
		return -EINVAL;
#endif
	return 0;
}

static void __exit test_exit(void)
{
}

/* Compile the generic mul_u64_add_u64_div_u64() code */
#undef __div64_32
#define __div64_32 __div64_32
#define div_s64_rem div_s64_rem
#define div64_u64_rem div64_u64_rem
#define div64_u64 div64_u64
#define div64_s64 div64_s64
#define iter_div_u64_rem iter_div_u64_rem

#undef mul_u64_add_u64_div_u64
#define mul_u64_add_u64_div_u64 test_mul_u64_add_u64_div_u64
#define test_mul_u64_add_u64_div_u64 test_mul_u64_add_u64_div_u64

#include "div64.c"

#ifdef TEST_32BIT_DIV
/* Recompile the generic code for 32bit long */
#undef test_mul_u64_add_u64_div_u64
#define test_mul_u64_add_u64_div_u64 test_mul_u64_add_u64_div_u64_32bit
#undef BITS_PER_ITER
#define BITS_PER_ITER 16

#define mul_u64_u64_add_u64 mul_u64_u64_add_u64_32bit
#undef mul_u64_long_add_u64
#undef add_u64_long
#undef mul_add

#include "div64.c"
#endif

module_init(test_init);
module_exit(test_exit);

MODULE_AUTHOR("Nicolas Pitre");
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("mul_u64_u64_div_u64() test module");
