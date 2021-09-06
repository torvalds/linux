// SPDX-License-Identifier: GPL-2.0-only
/*
 * Test cases for bitmap API.
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/bitmap.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/printk.h>
#include <linux/slab.h>
#include <linux/string.h>
#include <linux/uaccess.h>

#include "../tools/testing/selftests/kselftest_module.h"

KSTM_MODULE_GLOBALS();

static char pbl_buffer[PAGE_SIZE] __initdata;

static const unsigned long exp1[] __initconst = {
	BITMAP_FROM_U64(1),
	BITMAP_FROM_U64(2),
	BITMAP_FROM_U64(0x0000ffff),
	BITMAP_FROM_U64(0xffff0000),
	BITMAP_FROM_U64(0x55555555),
	BITMAP_FROM_U64(0xaaaaaaaa),
	BITMAP_FROM_U64(0x11111111),
	BITMAP_FROM_U64(0x22222222),
	BITMAP_FROM_U64(0xffffffff),
	BITMAP_FROM_U64(0xfffffffe),
	BITMAP_FROM_U64(0x3333333311111111ULL),
	BITMAP_FROM_U64(0xffffffff77777777ULL),
	BITMAP_FROM_U64(0),
	BITMAP_FROM_U64(0x00008000),
	BITMAP_FROM_U64(0x80000000),
};

static const unsigned long exp2[] __initconst = {
	BITMAP_FROM_U64(0x3333333311111111ULL),
	BITMAP_FROM_U64(0xffffffff77777777ULL),
};

/* Fibonacci sequence */
static const unsigned long exp2_to_exp3_mask[] __initconst = {
	BITMAP_FROM_U64(0x008000020020212eULL),
};
/* exp3_0_1 = (exp2[0] & ~exp2_to_exp3_mask) | (exp2[1] & exp2_to_exp3_mask) */
static const unsigned long exp3_0_1[] __initconst = {
	BITMAP_FROM_U64(0x33b3333311313137ULL),
};
/* exp3_1_0 = (exp2[1] & ~exp2_to_exp3_mask) | (exp2[0] & exp2_to_exp3_mask) */
static const unsigned long exp3_1_0[] __initconst = {
	BITMAP_FROM_U64(0xff7fffff77575751ULL),
};

static bool __init
__check_eq_uint(const char *srcfile, unsigned int line,
		const unsigned int exp_uint, unsigned int x)
{
	if (exp_uint != x) {
		pr_err("[%s:%u] expected %u, got %u\n",
			srcfile, line, exp_uint, x);
		return false;
	}
	return true;
}


static bool __init
__check_eq_bitmap(const char *srcfile, unsigned int line,
		  const unsigned long *exp_bmap, const unsigned long *bmap,
		  unsigned int nbits)
{
	if (!bitmap_equal(exp_bmap, bmap, nbits)) {
		pr_warn("[%s:%u] bitmaps contents differ: expected \"%*pbl\", got \"%*pbl\"\n",
			srcfile, line,
			nbits, exp_bmap, nbits, bmap);
		return false;
	}
	return true;
}

static bool __init
__check_eq_pbl(const char *srcfile, unsigned int line,
	       const char *expected_pbl,
	       const unsigned long *bitmap, unsigned int nbits)
{
	snprintf(pbl_buffer, sizeof(pbl_buffer), "%*pbl", nbits, bitmap);
	if (strcmp(expected_pbl, pbl_buffer)) {
		pr_warn("[%s:%u] expected \"%s\", got \"%s\"\n",
			srcfile, line,
			expected_pbl, pbl_buffer);
		return false;
	}
	return true;
}

static bool __init
__check_eq_u32_array(const char *srcfile, unsigned int line,
		     const u32 *exp_arr, unsigned int exp_len,
		     const u32 *arr, unsigned int len) __used;
static bool __init
__check_eq_u32_array(const char *srcfile, unsigned int line,
		     const u32 *exp_arr, unsigned int exp_len,
		     const u32 *arr, unsigned int len)
{
	if (exp_len != len) {
		pr_warn("[%s:%u] array length differ: expected %u, got %u\n",
			srcfile, line,
			exp_len, len);
		return false;
	}

	if (memcmp(exp_arr, arr, len*sizeof(*arr))) {
		pr_warn("[%s:%u] array contents differ\n", srcfile, line);
		print_hex_dump(KERN_WARNING, "  exp:  ", DUMP_PREFIX_OFFSET,
			       32, 4, exp_arr, exp_len*sizeof(*exp_arr), false);
		print_hex_dump(KERN_WARNING, "  got:  ", DUMP_PREFIX_OFFSET,
			       32, 4, arr, len*sizeof(*arr), false);
		return false;
	}

	return true;
}

static bool __init __check_eq_clump8(const char *srcfile, unsigned int line,
				    const unsigned int offset,
				    const unsigned int size,
				    const unsigned char *const clump_exp,
				    const unsigned long *const clump)
{
	unsigned long exp;

	if (offset >= size) {
		pr_warn("[%s:%u] bit offset for clump out-of-bounds: expected less than %u, got %u\n",
			srcfile, line, size, offset);
		return false;
	}

	exp = clump_exp[offset / 8];
	if (!exp) {
		pr_warn("[%s:%u] bit offset for zero clump: expected nonzero clump, got bit offset %u with clump value 0",
			srcfile, line, offset);
		return false;
	}

	if (*clump != exp) {
		pr_warn("[%s:%u] expected clump value of 0x%lX, got clump value of 0x%lX",
			srcfile, line, exp, *clump);
		return false;
	}

	return true;
}

#define __expect_eq(suffix, ...)					\
	({								\
		int result = 0;						\
		total_tests++;						\
		if (!__check_eq_ ## suffix(__FILE__, __LINE__,		\
					   ##__VA_ARGS__)) {		\
			failed_tests++;					\
			result = 1;					\
		}							\
		result;							\
	})

#define expect_eq_uint(...)		__expect_eq(uint, ##__VA_ARGS__)
#define expect_eq_bitmap(...)		__expect_eq(bitmap, ##__VA_ARGS__)
#define expect_eq_pbl(...)		__expect_eq(pbl, ##__VA_ARGS__)
#define expect_eq_u32_array(...)	__expect_eq(u32_array, ##__VA_ARGS__)
#define expect_eq_clump8(...)		__expect_eq(clump8, ##__VA_ARGS__)

static void __init test_zero_clear(void)
{
	DECLARE_BITMAP(bmap, 1024);

	/* Known way to set all bits */
	memset(bmap, 0xff, 128);

	expect_eq_pbl("0-22", bmap, 23);
	expect_eq_pbl("0-1023", bmap, 1024);

	/* single-word bitmaps */
	bitmap_clear(bmap, 0, 9);
	expect_eq_pbl("9-1023", bmap, 1024);

	bitmap_zero(bmap, 35);
	expect_eq_pbl("64-1023", bmap, 1024);

	/* cross boundaries operations */
	bitmap_clear(bmap, 79, 19);
	expect_eq_pbl("64-78,98-1023", bmap, 1024);

	bitmap_zero(bmap, 115);
	expect_eq_pbl("128-1023", bmap, 1024);

	/* Zeroing entire area */
	bitmap_zero(bmap, 1024);
	expect_eq_pbl("", bmap, 1024);
}

static void __init test_fill_set(void)
{
	DECLARE_BITMAP(bmap, 1024);

	/* Known way to clear all bits */
	memset(bmap, 0x00, 128);

	expect_eq_pbl("", bmap, 23);
	expect_eq_pbl("", bmap, 1024);

	/* single-word bitmaps */
	bitmap_set(bmap, 0, 9);
	expect_eq_pbl("0-8", bmap, 1024);

	bitmap_fill(bmap, 35);
	expect_eq_pbl("0-63", bmap, 1024);

	/* cross boundaries operations */
	bitmap_set(bmap, 79, 19);
	expect_eq_pbl("0-63,79-97", bmap, 1024);

	bitmap_fill(bmap, 115);
	expect_eq_pbl("0-127", bmap, 1024);

	/* Zeroing entire area */
	bitmap_fill(bmap, 1024);
	expect_eq_pbl("0-1023", bmap, 1024);
}

static void __init test_copy(void)
{
	DECLARE_BITMAP(bmap1, 1024);
	DECLARE_BITMAP(bmap2, 1024);

	bitmap_zero(bmap1, 1024);
	bitmap_zero(bmap2, 1024);

	/* single-word bitmaps */
	bitmap_set(bmap1, 0, 19);
	bitmap_copy(bmap2, bmap1, 23);
	expect_eq_pbl("0-18", bmap2, 1024);

	bitmap_set(bmap2, 0, 23);
	bitmap_copy(bmap2, bmap1, 23);
	expect_eq_pbl("0-18", bmap2, 1024);

	/* multi-word bitmaps */
	bitmap_set(bmap1, 0, 109);
	bitmap_copy(bmap2, bmap1, 1024);
	expect_eq_pbl("0-108", bmap2, 1024);

	bitmap_fill(bmap2, 1024);
	bitmap_copy(bmap2, bmap1, 1024);
	expect_eq_pbl("0-108", bmap2, 1024);

	/* the following tests assume a 32- or 64-bit arch (even 128b
	 * if we care)
	 */

	bitmap_fill(bmap2, 1024);
	bitmap_copy(bmap2, bmap1, 109);  /* ... but 0-padded til word length */
	expect_eq_pbl("0-108,128-1023", bmap2, 1024);

	bitmap_fill(bmap2, 1024);
	bitmap_copy(bmap2, bmap1, 97);  /* ... but aligned on word length */
	expect_eq_pbl("0-108,128-1023", bmap2, 1024);
}

#define EXP2_IN_BITS	(sizeof(exp2) * 8)

static void __init test_replace(void)
{
	unsigned int nbits = 64;
	unsigned int nlongs = DIV_ROUND_UP(nbits, BITS_PER_LONG);
	DECLARE_BITMAP(bmap, 1024);

	BUILD_BUG_ON(EXP2_IN_BITS < nbits * 2);

	bitmap_zero(bmap, 1024);
	bitmap_replace(bmap, &exp2[0 * nlongs], &exp2[1 * nlongs], exp2_to_exp3_mask, nbits);
	expect_eq_bitmap(bmap, exp3_0_1, nbits);

	bitmap_zero(bmap, 1024);
	bitmap_replace(bmap, &exp2[1 * nlongs], &exp2[0 * nlongs], exp2_to_exp3_mask, nbits);
	expect_eq_bitmap(bmap, exp3_1_0, nbits);

	bitmap_fill(bmap, 1024);
	bitmap_replace(bmap, &exp2[0 * nlongs], &exp2[1 * nlongs], exp2_to_exp3_mask, nbits);
	expect_eq_bitmap(bmap, exp3_0_1, nbits);

	bitmap_fill(bmap, 1024);
	bitmap_replace(bmap, &exp2[1 * nlongs], &exp2[0 * nlongs], exp2_to_exp3_mask, nbits);
	expect_eq_bitmap(bmap, exp3_1_0, nbits);
}

#define PARSE_TIME	0x1
#define NO_LEN		0x2

struct test_bitmap_parselist{
	const int errno;
	const char *in;
	const unsigned long *expected;
	const int nbits;
	const int flags;
};

static const struct test_bitmap_parselist parselist_tests[] __initconst = {
#define step (sizeof(u64) / sizeof(unsigned long))

	{0, "0",			&exp1[0], 8, 0},
	{0, "1",			&exp1[1 * step], 8, 0},
	{0, "0-15",			&exp1[2 * step], 32, 0},
	{0, "16-31",			&exp1[3 * step], 32, 0},
	{0, "0-31:1/2",			&exp1[4 * step], 32, 0},
	{0, "1-31:1/2",			&exp1[5 * step], 32, 0},
	{0, "0-31:1/4",			&exp1[6 * step], 32, 0},
	{0, "1-31:1/4",			&exp1[7 * step], 32, 0},
	{0, "0-31:4/4",			&exp1[8 * step], 32, 0},
	{0, "1-31:4/4",			&exp1[9 * step], 32, 0},
	{0, "0-31:1/4,32-63:2/4",	&exp1[10 * step], 64, 0},
	{0, "0-31:3/4,32-63:4/4",	&exp1[11 * step], 64, 0},
	{0, "  ,,  0-31:3/4  ,, 32-63:4/4  ,,  ",	&exp1[11 * step], 64, 0},

	{0, "0-31:1/4,32-63:2/4,64-95:3/4,96-127:4/4",	exp2, 128, 0},

	{0, "0-2047:128/256", NULL, 2048, PARSE_TIME},

	{0, "",				&exp1[12 * step], 8, 0},
	{0, "\n",			&exp1[12 * step], 8, 0},
	{0, ",,  ,,  , ,  ,",		&exp1[12 * step], 8, 0},
	{0, " ,  ,,  , ,   ",		&exp1[12 * step], 8, 0},
	{0, " ,  ,,  , ,   \n",		&exp1[12 * step], 8, 0},

	{0, "0-0",			&exp1[0], 32, 0},
	{0, "1-1",			&exp1[1 * step], 32, 0},
	{0, "15-15",			&exp1[13 * step], 32, 0},
	{0, "31-31",			&exp1[14 * step], 32, 0},

	{0, "0-0:0/1",			&exp1[12 * step], 32, 0},
	{0, "0-0:1/1",			&exp1[0], 32, 0},
	{0, "0-0:1/31",			&exp1[0], 32, 0},
	{0, "0-0:31/31",		&exp1[0], 32, 0},
	{0, "1-1:1/1",			&exp1[1 * step], 32, 0},
	{0, "0-15:16/31",		&exp1[2 * step], 32, 0},
	{0, "15-15:1/2",		&exp1[13 * step], 32, 0},
	{0, "15-15:31/31",		&exp1[13 * step], 32, 0},
	{0, "15-31:1/31",		&exp1[13 * step], 32, 0},
	{0, "16-31:16/31",		&exp1[3 * step], 32, 0},
	{0, "31-31:31/31",		&exp1[14 * step], 32, 0},

	{0, "N-N",			&exp1[14 * step], 32, 0},
	{0, "0-0:1/N",			&exp1[0], 32, 0},
	{0, "0-0:N/N",			&exp1[0], 32, 0},
	{0, "0-15:16/N",		&exp1[2 * step], 32, 0},
	{0, "15-15:N/N",		&exp1[13 * step], 32, 0},
	{0, "15-N:1/N",			&exp1[13 * step], 32, 0},
	{0, "16-N:16/N",		&exp1[3 * step], 32, 0},
	{0, "N-N:N/N",			&exp1[14 * step], 32, 0},

	{0, "0-N:1/3,1-N:1/3,2-N:1/3",		&exp1[8 * step], 32, 0},
	{0, "0-31:1/3,1-31:1/3,2-31:1/3",	&exp1[8 * step], 32, 0},
	{0, "1-10:8/12,8-31:24/29,0-31:0/3",	&exp1[9 * step], 32, 0},

	{-EINVAL, "-1",	NULL, 8, 0},
	{-EINVAL, "-0",	NULL, 8, 0},
	{-EINVAL, "10-1", NULL, 8, 0},
	{-ERANGE, "8-8", NULL, 8, 0},
	{-ERANGE, "0-31", NULL, 8, 0},
	{-EINVAL, "0-31:", NULL, 32, 0},
	{-EINVAL, "0-31:0", NULL, 32, 0},
	{-EINVAL, "0-31:0/", NULL, 32, 0},
	{-EINVAL, "0-31:0/0", NULL, 32, 0},
	{-EINVAL, "0-31:1/0", NULL, 32, 0},
	{-EINVAL, "0-31:10/1", NULL, 32, 0},
	{-EOVERFLOW, "0-98765432123456789:10/1", NULL, 8, 0},

	{-EINVAL, "a-31", NULL, 8, 0},
	{-EINVAL, "0-a1", NULL, 8, 0},
	{-EINVAL, "a-31:10/1", NULL, 8, 0},
	{-EINVAL, "0-31:a/1", NULL, 8, 0},
	{-EINVAL, "0-\n", NULL, 8, 0},

};

static void __init test_bitmap_parselist(void)
{
	int i;
	int err;
	ktime_t time;
	DECLARE_BITMAP(bmap, 2048);

	for (i = 0; i < ARRAY_SIZE(parselist_tests); i++) {
#define ptest parselist_tests[i]

		time = ktime_get();
		err = bitmap_parselist(ptest.in, bmap, ptest.nbits);
		time = ktime_get() - time;

		if (err != ptest.errno) {
			pr_err("parselist: %d: input is %s, errno is %d, expected %d\n",
					i, ptest.in, err, ptest.errno);
			continue;
		}

		if (!err && ptest.expected
			 && !__bitmap_equal(bmap, ptest.expected, ptest.nbits)) {
			pr_err("parselist: %d: input is %s, result is 0x%lx, expected 0x%lx\n",
					i, ptest.in, bmap[0],
					*ptest.expected);
			continue;
		}

		if (ptest.flags & PARSE_TIME)
			pr_err("parselist: %d: input is '%s' OK, Time: %llu\n",
					i, ptest.in, time);

#undef ptest
	}
}

static const unsigned long parse_test[] __initconst = {
	BITMAP_FROM_U64(0),
	BITMAP_FROM_U64(1),
	BITMAP_FROM_U64(0xdeadbeef),
	BITMAP_FROM_U64(0x100000000ULL),
};

static const unsigned long parse_test2[] __initconst = {
	BITMAP_FROM_U64(0x100000000ULL), BITMAP_FROM_U64(0xdeadbeef),
	BITMAP_FROM_U64(0x100000000ULL), BITMAP_FROM_U64(0xbaadf00ddeadbeef),
	BITMAP_FROM_U64(0x100000000ULL), BITMAP_FROM_U64(0x0badf00ddeadbeef),
};

static const struct test_bitmap_parselist parse_tests[] __initconst = {
	{0, "",				&parse_test[0 * step], 32, 0},
	{0, " ",			&parse_test[0 * step], 32, 0},
	{0, "0",			&parse_test[0 * step], 32, 0},
	{0, "0\n",			&parse_test[0 * step], 32, 0},
	{0, "1",			&parse_test[1 * step], 32, 0},
	{0, "deadbeef",			&parse_test[2 * step], 32, 0},
	{0, "1,0",			&parse_test[3 * step], 33, 0},
	{0, "deadbeef,\n,0,1",		&parse_test[2 * step], 96, 0},

	{0, "deadbeef,1,0",		&parse_test2[0 * 2 * step], 96, 0},
	{0, "baadf00d,deadbeef,1,0",	&parse_test2[1 * 2 * step], 128, 0},
	{0, "badf00d,deadbeef,1,0",	&parse_test2[2 * 2 * step], 124, 0},
	{0, "badf00d,deadbeef,1,0",	&parse_test2[2 * 2 * step], 124, NO_LEN},
	{0, "  badf00d,deadbeef,1,0  ",	&parse_test2[2 * 2 * step], 124, 0},
	{0, " , badf00d,deadbeef,1,0 , ",	&parse_test2[2 * 2 * step], 124, 0},
	{0, " , badf00d, ,, ,,deadbeef,1,0 , ",	&parse_test2[2 * 2 * step], 124, 0},

	{-EINVAL,    "goodfood,deadbeef,1,0",	NULL, 128, 0},
	{-EOVERFLOW, "3,0",			NULL, 33, 0},
	{-EOVERFLOW, "123badf00d,deadbeef,1,0",	NULL, 128, 0},
	{-EOVERFLOW, "badf00d,deadbeef,1,0",	NULL, 90, 0},
	{-EOVERFLOW, "fbadf00d,deadbeef,1,0",	NULL, 95, 0},
	{-EOVERFLOW, "badf00d,deadbeef,1,0",	NULL, 100, 0},
#undef step
};

static void __init test_bitmap_parse(void)
{
	int i;
	int err;
	ktime_t time;
	DECLARE_BITMAP(bmap, 2048);

	for (i = 0; i < ARRAY_SIZE(parse_tests); i++) {
		struct test_bitmap_parselist test = parse_tests[i];
		size_t len = test.flags & NO_LEN ? UINT_MAX : strlen(test.in);

		time = ktime_get();
		err = bitmap_parse(test.in, len, bmap, test.nbits);
		time = ktime_get() - time;

		if (err != test.errno) {
			pr_err("parse: %d: input is %s, errno is %d, expected %d\n",
					i, test.in, err, test.errno);
			continue;
		}

		if (!err && test.expected
			 && !__bitmap_equal(bmap, test.expected, test.nbits)) {
			pr_err("parse: %d: input is %s, result is 0x%lx, expected 0x%lx\n",
					i, test.in, bmap[0],
					*test.expected);
			continue;
		}

		if (test.flags & PARSE_TIME)
			pr_err("parse: %d: input is '%s' OK, Time: %llu\n",
					i, test.in, time);
	}
}

#define EXP1_IN_BITS	(sizeof(exp1) * 8)

static void __init test_bitmap_arr32(void)
{
	unsigned int nbits, next_bit;
	u32 arr[EXP1_IN_BITS / 32];
	DECLARE_BITMAP(bmap2, EXP1_IN_BITS);

	memset(arr, 0xa5, sizeof(arr));

	for (nbits = 0; nbits < EXP1_IN_BITS; ++nbits) {
		bitmap_to_arr32(arr, exp1, nbits);
		bitmap_from_arr32(bmap2, arr, nbits);
		expect_eq_bitmap(bmap2, exp1, nbits);

		next_bit = find_next_bit(bmap2,
				round_up(nbits, BITS_PER_LONG), nbits);
		if (next_bit < round_up(nbits, BITS_PER_LONG))
			pr_err("bitmap_copy_arr32(nbits == %d:"
				" tail is not safely cleared: %d\n",
				nbits, next_bit);

		if (nbits < EXP1_IN_BITS - 32)
			expect_eq_uint(arr[DIV_ROUND_UP(nbits, 32)],
								0xa5a5a5a5);
	}
}

static void noinline __init test_mem_optimisations(void)
{
	DECLARE_BITMAP(bmap1, 1024);
	DECLARE_BITMAP(bmap2, 1024);
	unsigned int start, nbits;

	for (start = 0; start < 1024; start += 8) {
		for (nbits = 0; nbits < 1024 - start; nbits += 8) {
			memset(bmap1, 0x5a, sizeof(bmap1));
			memset(bmap2, 0x5a, sizeof(bmap2));

			bitmap_set(bmap1, start, nbits);
			__bitmap_set(bmap2, start, nbits);
			if (!bitmap_equal(bmap1, bmap2, 1024)) {
				printk("set not equal %d %d\n", start, nbits);
				failed_tests++;
			}
			if (!__bitmap_equal(bmap1, bmap2, 1024)) {
				printk("set not __equal %d %d\n", start, nbits);
				failed_tests++;
			}

			bitmap_clear(bmap1, start, nbits);
			__bitmap_clear(bmap2, start, nbits);
			if (!bitmap_equal(bmap1, bmap2, 1024)) {
				printk("clear not equal %d %d\n", start, nbits);
				failed_tests++;
			}
			if (!__bitmap_equal(bmap1, bmap2, 1024)) {
				printk("clear not __equal %d %d\n", start,
									nbits);
				failed_tests++;
			}
		}
	}
}

static const unsigned char clump_exp[] __initconst = {
	0x01,	/* 1 bit set */
	0x02,	/* non-edge 1 bit set */
	0x00,	/* zero bits set */
	0x38,	/* 3 bits set across 4-bit boundary */
	0x38,	/* Repeated clump */
	0x0F,	/* 4 bits set */
	0xFF,	/* all bits set */
	0x05,	/* non-adjacent 2 bits set */
};

static void __init test_for_each_set_clump8(void)
{
#define CLUMP_EXP_NUMBITS 64
	DECLARE_BITMAP(bits, CLUMP_EXP_NUMBITS);
	unsigned int start;
	unsigned long clump;

	/* set bitmap to test case */
	bitmap_zero(bits, CLUMP_EXP_NUMBITS);
	bitmap_set(bits, 0, 1);		/* 0x01 */
	bitmap_set(bits, 9, 1);		/* 0x02 */
	bitmap_set(bits, 27, 3);	/* 0x28 */
	bitmap_set(bits, 35, 3);	/* 0x28 */
	bitmap_set(bits, 40, 4);	/* 0x0F */
	bitmap_set(bits, 48, 8);	/* 0xFF */
	bitmap_set(bits, 56, 1);	/* 0x05 - part 1 */
	bitmap_set(bits, 58, 1);	/* 0x05 - part 2 */

	for_each_set_clump8(start, clump, bits, CLUMP_EXP_NUMBITS)
		expect_eq_clump8(start, CLUMP_EXP_NUMBITS, clump_exp, &clump);
}

struct test_bitmap_cut {
	unsigned int first;
	unsigned int cut;
	unsigned int nbits;
	unsigned long in[4];
	unsigned long expected[4];
};

static struct test_bitmap_cut test_cut[] = {
	{  0,  0,  8, { 0x0000000aUL, }, { 0x0000000aUL, }, },
	{  0,  0, 32, { 0xdadadeadUL, }, { 0xdadadeadUL, }, },
	{  0,  3,  8, { 0x000000aaUL, }, { 0x00000015UL, }, },
	{  3,  3,  8, { 0x000000aaUL, }, { 0x00000012UL, }, },
	{  0,  1, 32, { 0xa5a5a5a5UL, }, { 0x52d2d2d2UL, }, },
	{  0,  8, 32, { 0xdeadc0deUL, }, { 0x00deadc0UL, }, },
	{  1,  1, 32, { 0x5a5a5a5aUL, }, { 0x2d2d2d2cUL, }, },
	{  0, 15, 32, { 0xa5a5a5a5UL, }, { 0x00014b4bUL, }, },
	{  0, 16, 32, { 0xa5a5a5a5UL, }, { 0x0000a5a5UL, }, },
	{ 15, 15, 32, { 0xa5a5a5a5UL, }, { 0x000125a5UL, }, },
	{ 15, 16, 32, { 0xa5a5a5a5UL, }, { 0x0000a5a5UL, }, },
	{ 16, 15, 32, { 0xa5a5a5a5UL, }, { 0x0001a5a5UL, }, },

	{ BITS_PER_LONG, BITS_PER_LONG, BITS_PER_LONG,
		{ 0xa5a5a5a5UL, 0xa5a5a5a5UL, },
		{ 0xa5a5a5a5UL, 0xa5a5a5a5UL, },
	},
	{ 1, BITS_PER_LONG - 1, BITS_PER_LONG,
		{ 0xa5a5a5a5UL, 0xa5a5a5a5UL, },
		{ 0x00000001UL, 0x00000001UL, },
	},

	{ 0, BITS_PER_LONG * 2, BITS_PER_LONG * 2 + 1,
		{ 0xa5a5a5a5UL, 0x00000001UL, 0x00000001UL, 0x00000001UL },
		{ 0x00000001UL, },
	},
	{ 16, BITS_PER_LONG * 2 + 1, BITS_PER_LONG * 2 + 1 + 16,
		{ 0x0000ffffUL, 0x5a5a5a5aUL, 0x5a5a5a5aUL, 0x5a5a5a5aUL },
		{ 0x2d2dffffUL, },
	},
};

static void __init test_bitmap_cut(void)
{
	unsigned long b[5], *in = &b[1], *out = &b[0];	/* Partial overlap */
	int i;

	for (i = 0; i < ARRAY_SIZE(test_cut); i++) {
		struct test_bitmap_cut *t = &test_cut[i];

		memcpy(in, t->in, sizeof(t->in));

		bitmap_cut(out, in, t->first, t->cut, t->nbits);

		expect_eq_bitmap(t->expected, out, t->nbits);
	}
}

static void __init selftest(void)
{
	test_zero_clear();
	test_fill_set();
	test_copy();
	test_replace();
	test_bitmap_arr32();
	test_bitmap_parse();
	test_bitmap_parselist();
	test_mem_optimisations();
	test_for_each_set_clump8();
	test_bitmap_cut();
}

KSTM_MODULE_LOADERS(test_bitmap);
MODULE_AUTHOR("david decotigny <david.decotigny@googlers.com>");
MODULE_LICENSE("GPL");
