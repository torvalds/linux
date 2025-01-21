// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2024 BayLibre SAS
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/init.h>
#include <linux/module.h>
#include <linux/printk.h>
#include <linux/math64.h>

typedef struct { u64 a; u64 b; u64 c; u64 result; } test_params;

static test_params test_values[] = {
/* this contains many edge values followed by a couple random values */
{                0xb,                0x7,                0x3,               0x19 },
{         0xffff0000,         0xffff0000,                0xf, 0x1110eeef00000000 },
{         0xffffffff,         0xffffffff,                0x1, 0xfffffffe00000001 },
{         0xffffffff,         0xffffffff,                0x2, 0x7fffffff00000000 },
{        0x1ffffffff,         0xffffffff,                0x2, 0xfffffffe80000000 },
{        0x1ffffffff,         0xffffffff,                0x3, 0xaaaaaaa9aaaaaaab },
{        0x1ffffffff,        0x1ffffffff,                0x4, 0xffffffff00000000 },
{ 0xffff000000000000, 0xffff000000000000, 0xffff000000000001, 0xfffeffffffffffff },
{ 0x3333333333333333, 0x3333333333333333, 0x5555555555555555, 0x1eb851eb851eb851 },
{ 0x7fffffffffffffff,                0x2,                0x3, 0x5555555555555554 },
{ 0xffffffffffffffff,                0x2, 0x8000000000000000,                0x3 },
{ 0xffffffffffffffff,                0x2, 0xc000000000000000,                0x2 },
{ 0xffffffffffffffff, 0x4000000000000004, 0x8000000000000000, 0x8000000000000007 },
{ 0xffffffffffffffff, 0x4000000000000001, 0x8000000000000000, 0x8000000000000001 },
{ 0xffffffffffffffff, 0x8000000000000001, 0xffffffffffffffff, 0x8000000000000001 },
{ 0xfffffffffffffffe, 0x8000000000000001, 0xffffffffffffffff, 0x8000000000000000 },
{ 0xffffffffffffffff, 0x8000000000000001, 0xfffffffffffffffe, 0x8000000000000001 },
{ 0xffffffffffffffff, 0x8000000000000001, 0xfffffffffffffffd, 0x8000000000000002 },
{ 0x7fffffffffffffff, 0xffffffffffffffff, 0xc000000000000000, 0xaaaaaaaaaaaaaaa8 },
{ 0xffffffffffffffff, 0x7fffffffffffffff, 0xa000000000000000, 0xccccccccccccccca },
{ 0xffffffffffffffff, 0x7fffffffffffffff, 0x9000000000000000, 0xe38e38e38e38e38b },
{ 0x7fffffffffffffff, 0x7fffffffffffffff, 0x5000000000000000, 0xccccccccccccccc9 },
{ 0xffffffffffffffff, 0xfffffffffffffffe, 0xffffffffffffffff, 0xfffffffffffffffe },
{ 0xe6102d256d7ea3ae, 0x70a77d0be4c31201, 0xd63ec35ab3220357, 0x78f8bf8cc86c6e18 },
{ 0xf53bae05cb86c6e1, 0x3847b32d2f8d32e0, 0xcfd4f55a647f403c, 0x42687f79d8998d35 },
{ 0x9951c5498f941092, 0x1f8c8bfdf287a251, 0xa3c8dc5f81ea3fe2, 0x1d887cb25900091f },
{ 0x374fee9daa1bb2bb, 0x0d0bfbff7b8ae3ef, 0xc169337bd42d5179, 0x03bb2dbaffcbb961 },
{ 0xeac0d03ac10eeaf0, 0x89be05dfa162ed9b, 0x92bb1679a41f0e4b, 0xdc5f5cc9e270d216 },
};

/*
 * The above table can be verified with the following shell script:
 *
 * #!/bin/sh
 * sed -ne 's/^{ \+\(.*\), \+\(.*\), \+\(.*\), \+\(.*\) },$/\1 \2 \3 \4/p' \
 *     lib/math/test_mul_u64_u64_div_u64.c |
 * while read a b c r; do
 *   expected=$( printf "obase=16; ibase=16; %X * %X / %X\n" $a $b $c | bc )
 *   given=$( printf "%X\n" $r )
 *   if [ "$expected" = "$given" ]; then
 *     echo "$a * $b / $c = $r OK"
 *   else
 *     echo "$a * $b / $c = $r is wrong" >&2
 *     echo "should be equivalent to 0x$expected" >&2
 *     exit 1
 *   fi
 * done
 */

static int __init test_init(void)
{
	int i;

	pr_info("Starting mul_u64_u64_div_u64() test\n");

	for (i = 0; i < ARRAY_SIZE(test_values); i++) {
		u64 a = test_values[i].a;
		u64 b = test_values[i].b;
		u64 c = test_values[i].c;
		u64 expected_result = test_values[i].result;
		u64 result = mul_u64_u64_div_u64(a, b, c);

		if (result != expected_result) {
			pr_err("ERROR: 0x%016llx * 0x%016llx / 0x%016llx\n", a, b, c);
			pr_err("ERROR: expected result: %016llx\n", expected_result);
			pr_err("ERROR: obtained result: %016llx\n", result);
		}
	}

	pr_info("Completed mul_u64_u64_div_u64() test\n");
	return 0;
}

static void __exit test_exit(void)
{
}

module_init(test_init);
module_exit(test_exit);

MODULE_AUTHOR("Nicolas Pitre");
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("mul_u64_u64_div_u64() test module");
