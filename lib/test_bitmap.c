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

#define EXP1_IN_BITS	(sizeof(exp1) * 8)

KSTM_MODULE_GLOBALS();

static char pbl_buffer[PAGE_SIZE] __initdata;
static char print_buf[PAGE_SIZE * 2] __initdata;

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
__check_eq_ulong(const char *srcfile, unsigned int line,
		 const unsigned long exp_ulong, unsigned long x)
{
	if (exp_ulong != x) {
		pr_err("[%s:%u] expected %lu, got %lu\n",
			srcfile, line, exp_ulong, x);
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

static bool __init
__check_eq_str(const char *srcfile, unsigned int line,
		const char *exp_str, const char *str,
		unsigned int len)
{
	bool eq;

	eq = strncmp(exp_str, str, len) == 0;
	if (!eq)
		pr_err("[%s:%u] expected %s, got %s\n", srcfile, line, exp_str, str);

	return eq;
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

#define expect_eq_ulong(...)		__expect_eq(ulong, ##__VA_ARGS__)
#define expect_eq_uint(x, y)		expect_eq_ulong((unsigned int)(x), (unsigned int)(y))
#define expect_eq_bitmap(...)		__expect_eq(bitmap, ##__VA_ARGS__)
#define expect_eq_pbl(...)		__expect_eq(pbl, ##__VA_ARGS__)
#define expect_eq_u32_array(...)	__expect_eq(u32_array, ##__VA_ARGS__)
#define expect_eq_clump8(...)		__expect_eq(clump8, ##__VA_ARGS__)
#define expect_eq_str(...)		__expect_eq(str, ##__VA_ARGS__)

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

static void __init test_find_nth_bit(void)
{
	unsigned long b, bit, cnt = 0;
	DECLARE_BITMAP(bmap, 64 * 3);

	bitmap_zero(bmap, 64 * 3);
	__set_bit(10, bmap);
	__set_bit(20, bmap);
	__set_bit(30, bmap);
	__set_bit(40, bmap);
	__set_bit(50, bmap);
	__set_bit(60, bmap);
	__set_bit(80, bmap);
	__set_bit(123, bmap);

	expect_eq_uint(10,  find_nth_bit(bmap, 64 * 3, 0));
	expect_eq_uint(20,  find_nth_bit(bmap, 64 * 3, 1));
	expect_eq_uint(30,  find_nth_bit(bmap, 64 * 3, 2));
	expect_eq_uint(40,  find_nth_bit(bmap, 64 * 3, 3));
	expect_eq_uint(50,  find_nth_bit(bmap, 64 * 3, 4));
	expect_eq_uint(60,  find_nth_bit(bmap, 64 * 3, 5));
	expect_eq_uint(80,  find_nth_bit(bmap, 64 * 3, 6));
	expect_eq_uint(123, find_nth_bit(bmap, 64 * 3, 7));
	expect_eq_uint(0,   !!(find_nth_bit(bmap, 64 * 3, 8) < 64 * 3));

	expect_eq_uint(10,  find_nth_bit(bmap, 64 * 3 - 1, 0));
	expect_eq_uint(20,  find_nth_bit(bmap, 64 * 3 - 1, 1));
	expect_eq_uint(30,  find_nth_bit(bmap, 64 * 3 - 1, 2));
	expect_eq_uint(40,  find_nth_bit(bmap, 64 * 3 - 1, 3));
	expect_eq_uint(50,  find_nth_bit(bmap, 64 * 3 - 1, 4));
	expect_eq_uint(60,  find_nth_bit(bmap, 64 * 3 - 1, 5));
	expect_eq_uint(80,  find_nth_bit(bmap, 64 * 3 - 1, 6));
	expect_eq_uint(123, find_nth_bit(bmap, 64 * 3 - 1, 7));
	expect_eq_uint(0,   !!(find_nth_bit(bmap, 64 * 3 - 1, 8) < 64 * 3 - 1));

	for_each_set_bit(bit, exp1, EXP1_IN_BITS) {
		b = find_nth_bit(exp1, EXP1_IN_BITS, cnt++);
		expect_eq_uint(b, bit);
	}
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

static void __init test_bitmap_region(void)
{
	int pos, order;

	DECLARE_BITMAP(bmap, 1000);

	bitmap_zero(bmap, 1000);

	for (order = 0; order < 10; order++) {
		pos = bitmap_find_free_region(bmap, 1000, order);
		if (order == 0)
			expect_eq_uint(pos, 0);
		else
			expect_eq_uint(pos, order < 9 ? BIT(order) : -ENOMEM);
	}

	bitmap_release_region(bmap, 0, 0);
	for (order = 1; order < 9; order++)
		bitmap_release_region(bmap, BIT(order), order);

	expect_eq_uint(bitmap_weight(bmap, 1000), 0);
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

static const unsigned long sg_mask[] __initconst = {
	BITMAP_FROM_U64(0x000000000000035aULL),
};

static const unsigned long sg_src[] __initconst = {
	BITMAP_FROM_U64(0x0000000000000667ULL),
};

static const unsigned long sg_gather_exp[] __initconst = {
	BITMAP_FROM_U64(0x0000000000000029ULL),
};

static const unsigned long sg_scatter_exp[] __initconst = {
	BITMAP_FROM_U64(0x000000000000021aULL),
};

static void __init test_bitmap_sg(void)
{
	unsigned int nbits = 64;
	DECLARE_BITMAP(bmap_gather, 100);
	DECLARE_BITMAP(bmap_scatter, 100);
	DECLARE_BITMAP(bmap_tmp, 100);
	DECLARE_BITMAP(bmap_res, 100);

	/* Simple gather call */
	bitmap_zero(bmap_gather, 100);
	bitmap_gather(bmap_gather, sg_src, sg_mask, nbits);
	expect_eq_bitmap(sg_gather_exp, bmap_gather, nbits);

	/* Simple scatter call */
	bitmap_zero(bmap_scatter, 100);
	bitmap_scatter(bmap_scatter, sg_src, sg_mask, nbits);
	expect_eq_bitmap(sg_scatter_exp, bmap_scatter, nbits);

	/* Scatter/gather relationship */
	bitmap_zero(bmap_tmp, 100);
	bitmap_gather(bmap_tmp, bmap_scatter, sg_mask, nbits);
	bitmap_scatter(bmap_res, bmap_tmp, sg_mask, nbits);
	expect_eq_bitmap(bmap_scatter, bmap_res, nbits);
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

	{0,	  "all",		&exp1[8 * step], 32, 0},
	{0,	  "0, 1, all,  ",	&exp1[8 * step], 32, 0},
	{0,	  "all:1/2",		&exp1[4 * step], 32, 0},
	{0,	  "ALL:1/2",		&exp1[4 * step], 32, 0},
	{-EINVAL, "al", NULL, 8, 0},
	{-EINVAL, "alll", NULL, 8, 0},

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
			failed_tests++;
			continue;
		}

		if (!err && ptest.expected
			 && !__bitmap_equal(bmap, ptest.expected, ptest.nbits)) {
			pr_err("parselist: %d: input is %s, result is 0x%lx, expected 0x%lx\n",
					i, ptest.in, bmap[0],
					*ptest.expected);
			failed_tests++;
			continue;
		}

		if (ptest.flags & PARSE_TIME)
			pr_info("parselist: %d: input is '%s' OK, Time: %llu\n",
					i, ptest.in, time);

#undef ptest
	}
}

static void __init test_bitmap_printlist(void)
{
	unsigned long *bmap = kmalloc(PAGE_SIZE, GFP_KERNEL);
	char *buf = kmalloc(PAGE_SIZE, GFP_KERNEL);
	char expected[256];
	int ret, slen;
	ktime_t time;

	if (!buf || !bmap)
		goto out;

	memset(bmap, -1, PAGE_SIZE);
	slen = snprintf(expected, 256, "0-%ld", PAGE_SIZE * 8 - 1);
	if (slen < 0)
		goto out;

	time = ktime_get();
	ret = bitmap_print_to_pagebuf(true, buf, bmap, PAGE_SIZE * 8);
	time = ktime_get() - time;

	if (ret != slen + 1) {
		pr_err("bitmap_print_to_pagebuf: result is %d, expected %d\n", ret, slen);
		failed_tests++;
		goto out;
	}

	if (strncmp(buf, expected, slen)) {
		pr_err("bitmap_print_to_pagebuf: result is %s, expected %s\n", buf, expected);
		failed_tests++;
		goto out;
	}

	pr_info("bitmap_print_to_pagebuf: input is '%s', Time: %llu\n", buf, time);
out:
	kfree(buf);
	kfree(bmap);
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
			failed_tests++;
			continue;
		}

		if (!err && test.expected
			 && !__bitmap_equal(bmap, test.expected, test.nbits)) {
			pr_err("parse: %d: input is %s, result is 0x%lx, expected 0x%lx\n",
					i, test.in, bmap[0],
					*test.expected);
			failed_tests++;
			continue;
		}

		if (test.flags & PARSE_TIME)
			pr_info("parse: %d: input is '%s' OK, Time: %llu\n",
					i, test.in, time);
	}
}

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
		if (next_bit < round_up(nbits, BITS_PER_LONG)) {
			pr_err("bitmap_copy_arr32(nbits == %d:"
				" tail is not safely cleared: %d\n",
				nbits, next_bit);
			failed_tests++;
		}

		if (nbits < EXP1_IN_BITS - 32)
			expect_eq_uint(arr[DIV_ROUND_UP(nbits, 32)],
								0xa5a5a5a5);
	}
}

static void __init test_bitmap_arr64(void)
{
	unsigned int nbits, next_bit;
	u64 arr[EXP1_IN_BITS / 64];
	DECLARE_BITMAP(bmap2, EXP1_IN_BITS);

	memset(arr, 0xa5, sizeof(arr));

	for (nbits = 0; nbits < EXP1_IN_BITS; ++nbits) {
		memset(bmap2, 0xff, sizeof(arr));
		bitmap_to_arr64(arr, exp1, nbits);
		bitmap_from_arr64(bmap2, arr, nbits);
		expect_eq_bitmap(bmap2, exp1, nbits);

		next_bit = find_next_bit(bmap2, round_up(nbits, BITS_PER_LONG), nbits);
		if (next_bit < round_up(nbits, BITS_PER_LONG)) {
			pr_err("bitmap_copy_arr64(nbits == %d:"
				" tail is not safely cleared: %d\n", nbits, next_bit);
			failed_tests++;
		}

		if ((nbits % 64) &&
		    (arr[(nbits - 1) / 64] & ~GENMASK_ULL((nbits - 1) % 64, 0))) {
			pr_err("bitmap_to_arr64(nbits == %d): tail is not safely cleared: 0x%016llx (must be 0x%016llx)\n",
			       nbits, arr[(nbits - 1) / 64],
			       GENMASK_ULL((nbits - 1) % 64, 0));
			failed_tests++;
		}

		if (nbits < EXP1_IN_BITS - 64)
			expect_eq_uint(arr[DIV_ROUND_UP(nbits, 64)], 0xa5a5a5a5);
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

static void __init test_for_each_set_bit_wrap(void)
{
	DECLARE_BITMAP(orig, 500);
	DECLARE_BITMAP(copy, 500);
	unsigned int wr, bit;

	bitmap_zero(orig, 500);

	/* Set individual bits */
	for (bit = 0; bit < 500; bit += 10)
		bitmap_set(orig, bit, 1);

	/* Set range of bits */
	bitmap_set(orig, 100, 50);

	for (wr = 0; wr < 500; wr++) {
		bitmap_zero(copy, 500);

		for_each_set_bit_wrap(bit, orig, 500, wr)
			bitmap_set(copy, bit, 1);

		expect_eq_bitmap(orig, copy, 500);
	}
}

static void __init test_for_each_set_bit(void)
{
	DECLARE_BITMAP(orig, 500);
	DECLARE_BITMAP(copy, 500);
	unsigned int bit;

	bitmap_zero(orig, 500);
	bitmap_zero(copy, 500);

	/* Set individual bits */
	for (bit = 0; bit < 500; bit += 10)
		bitmap_set(orig, bit, 1);

	/* Set range of bits */
	bitmap_set(orig, 100, 50);

	for_each_set_bit(bit, orig, 500)
		bitmap_set(copy, bit, 1);

	expect_eq_bitmap(orig, copy, 500);
}

static void __init test_for_each_set_bit_from(void)
{
	DECLARE_BITMAP(orig, 500);
	DECLARE_BITMAP(copy, 500);
	unsigned int wr, bit;

	bitmap_zero(orig, 500);

	/* Set individual bits */
	for (bit = 0; bit < 500; bit += 10)
		bitmap_set(orig, bit, 1);

	/* Set range of bits */
	bitmap_set(orig, 100, 50);

	for (wr = 0; wr < 500; wr++) {
		DECLARE_BITMAP(tmp, 500);

		bitmap_zero(copy, 500);
		bit = wr;

		for_each_set_bit_from(bit, orig, 500)
			bitmap_set(copy, bit, 1);

		bitmap_copy(tmp, orig, 500);
		bitmap_clear(tmp, 0, wr);
		expect_eq_bitmap(tmp, copy, 500);
	}
}

static void __init test_for_each_clear_bit(void)
{
	DECLARE_BITMAP(orig, 500);
	DECLARE_BITMAP(copy, 500);
	unsigned int bit;

	bitmap_fill(orig, 500);
	bitmap_fill(copy, 500);

	/* Set individual bits */
	for (bit = 0; bit < 500; bit += 10)
		bitmap_clear(orig, bit, 1);

	/* Set range of bits */
	bitmap_clear(orig, 100, 50);

	for_each_clear_bit(bit, orig, 500)
		bitmap_clear(copy, bit, 1);

	expect_eq_bitmap(orig, copy, 500);
}

static void __init test_for_each_clear_bit_from(void)
{
	DECLARE_BITMAP(orig, 500);
	DECLARE_BITMAP(copy, 500);
	unsigned int wr, bit;

	bitmap_fill(orig, 500);

	/* Set individual bits */
	for (bit = 0; bit < 500; bit += 10)
		bitmap_clear(orig, bit, 1);

	/* Set range of bits */
	bitmap_clear(orig, 100, 50);

	for (wr = 0; wr < 500; wr++) {
		DECLARE_BITMAP(tmp, 500);

		bitmap_fill(copy, 500);
		bit = wr;

		for_each_clear_bit_from(bit, orig, 500)
			bitmap_clear(copy, bit, 1);

		bitmap_copy(tmp, orig, 500);
		bitmap_set(tmp, 0, wr);
		expect_eq_bitmap(tmp, copy, 500);
	}
}

static void __init test_for_each_set_bitrange(void)
{
	DECLARE_BITMAP(orig, 500);
	DECLARE_BITMAP(copy, 500);
	unsigned int s, e;

	bitmap_zero(orig, 500);
	bitmap_zero(copy, 500);

	/* Set individual bits */
	for (s = 0; s < 500; s += 10)
		bitmap_set(orig, s, 1);

	/* Set range of bits */
	bitmap_set(orig, 100, 50);

	for_each_set_bitrange(s, e, orig, 500)
		bitmap_set(copy, s, e-s);

	expect_eq_bitmap(orig, copy, 500);
}

static void __init test_for_each_clear_bitrange(void)
{
	DECLARE_BITMAP(orig, 500);
	DECLARE_BITMAP(copy, 500);
	unsigned int s, e;

	bitmap_fill(orig, 500);
	bitmap_fill(copy, 500);

	/* Set individual bits */
	for (s = 0; s < 500; s += 10)
		bitmap_clear(orig, s, 1);

	/* Set range of bits */
	bitmap_clear(orig, 100, 50);

	for_each_clear_bitrange(s, e, orig, 500)
		bitmap_clear(copy, s, e-s);

	expect_eq_bitmap(orig, copy, 500);
}

static void __init test_for_each_set_bitrange_from(void)
{
	DECLARE_BITMAP(orig, 500);
	DECLARE_BITMAP(copy, 500);
	unsigned int wr, s, e;

	bitmap_zero(orig, 500);

	/* Set individual bits */
	for (s = 0; s < 500; s += 10)
		bitmap_set(orig, s, 1);

	/* Set range of bits */
	bitmap_set(orig, 100, 50);

	for (wr = 0; wr < 500; wr++) {
		DECLARE_BITMAP(tmp, 500);

		bitmap_zero(copy, 500);
		s = wr;

		for_each_set_bitrange_from(s, e, orig, 500)
			bitmap_set(copy, s, e - s);

		bitmap_copy(tmp, orig, 500);
		bitmap_clear(tmp, 0, wr);
		expect_eq_bitmap(tmp, copy, 500);
	}
}

static void __init test_for_each_clear_bitrange_from(void)
{
	DECLARE_BITMAP(orig, 500);
	DECLARE_BITMAP(copy, 500);
	unsigned int wr, s, e;

	bitmap_fill(orig, 500);

	/* Set individual bits */
	for (s = 0; s < 500; s += 10)
		bitmap_clear(orig, s, 1);

	/* Set range of bits */
	bitmap_set(orig, 100, 50);

	for (wr = 0; wr < 500; wr++) {
		DECLARE_BITMAP(tmp, 500);

		bitmap_fill(copy, 500);
		s = wr;

		for_each_clear_bitrange_from(s, e, orig, 500)
			bitmap_clear(copy, s, e - s);

		bitmap_copy(tmp, orig, 500);
		bitmap_set(tmp, 0, wr);
		expect_eq_bitmap(tmp, copy, 500);
	}
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

struct test_bitmap_print {
	const unsigned long *bitmap;
	unsigned long nbits;
	const char *mask;
	const char *list;
};

static const unsigned long small_bitmap[] __initconst = {
	BITMAP_FROM_U64(0x3333333311111111ULL),
};

static const char small_mask[] __initconst = "33333333,11111111\n";
static const char small_list[] __initconst = "0,4,8,12,16,20,24,28,32-33,36-37,40-41,44-45,48-49,52-53,56-57,60-61\n";

static const unsigned long large_bitmap[] __initconst = {
	BITMAP_FROM_U64(0x3333333311111111ULL), BITMAP_FROM_U64(0x3333333311111111ULL),
	BITMAP_FROM_U64(0x3333333311111111ULL), BITMAP_FROM_U64(0x3333333311111111ULL),
	BITMAP_FROM_U64(0x3333333311111111ULL), BITMAP_FROM_U64(0x3333333311111111ULL),
	BITMAP_FROM_U64(0x3333333311111111ULL), BITMAP_FROM_U64(0x3333333311111111ULL),
	BITMAP_FROM_U64(0x3333333311111111ULL), BITMAP_FROM_U64(0x3333333311111111ULL),
	BITMAP_FROM_U64(0x3333333311111111ULL), BITMAP_FROM_U64(0x3333333311111111ULL),
	BITMAP_FROM_U64(0x3333333311111111ULL), BITMAP_FROM_U64(0x3333333311111111ULL),
	BITMAP_FROM_U64(0x3333333311111111ULL), BITMAP_FROM_U64(0x3333333311111111ULL),
	BITMAP_FROM_U64(0x3333333311111111ULL), BITMAP_FROM_U64(0x3333333311111111ULL),
	BITMAP_FROM_U64(0x3333333311111111ULL), BITMAP_FROM_U64(0x3333333311111111ULL),
	BITMAP_FROM_U64(0x3333333311111111ULL), BITMAP_FROM_U64(0x3333333311111111ULL),
	BITMAP_FROM_U64(0x3333333311111111ULL), BITMAP_FROM_U64(0x3333333311111111ULL),
	BITMAP_FROM_U64(0x3333333311111111ULL), BITMAP_FROM_U64(0x3333333311111111ULL),
	BITMAP_FROM_U64(0x3333333311111111ULL), BITMAP_FROM_U64(0x3333333311111111ULL),
	BITMAP_FROM_U64(0x3333333311111111ULL), BITMAP_FROM_U64(0x3333333311111111ULL),
	BITMAP_FROM_U64(0x3333333311111111ULL), BITMAP_FROM_U64(0x3333333311111111ULL),
	BITMAP_FROM_U64(0x3333333311111111ULL), BITMAP_FROM_U64(0x3333333311111111ULL),
	BITMAP_FROM_U64(0x3333333311111111ULL), BITMAP_FROM_U64(0x3333333311111111ULL),
	BITMAP_FROM_U64(0x3333333311111111ULL), BITMAP_FROM_U64(0x3333333311111111ULL),
	BITMAP_FROM_U64(0x3333333311111111ULL), BITMAP_FROM_U64(0x3333333311111111ULL),
};

static const char large_mask[] __initconst = "33333333,11111111,33333333,11111111,"
					"33333333,11111111,33333333,11111111,"
					"33333333,11111111,33333333,11111111,"
					"33333333,11111111,33333333,11111111,"
					"33333333,11111111,33333333,11111111,"
					"33333333,11111111,33333333,11111111,"
					"33333333,11111111,33333333,11111111,"
					"33333333,11111111,33333333,11111111,"
					"33333333,11111111,33333333,11111111,"
					"33333333,11111111,33333333,11111111,"
					"33333333,11111111,33333333,11111111,"
					"33333333,11111111,33333333,11111111,"
					"33333333,11111111,33333333,11111111,"
					"33333333,11111111,33333333,11111111,"
					"33333333,11111111,33333333,11111111,"
					"33333333,11111111,33333333,11111111,"
					"33333333,11111111,33333333,11111111,"
					"33333333,11111111,33333333,11111111,"
					"33333333,11111111,33333333,11111111,"
					"33333333,11111111,33333333,11111111\n";

static const char large_list[] __initconst = /* more than 4KB */
	"0,4,8,12,16,20,24,28,32-33,36-37,40-41,44-45,48-49,52-53,56-57,60-61,64,68,72,76,80,84,88,92,96-97,100-101,104-1"
	"05,108-109,112-113,116-117,120-121,124-125,128,132,136,140,144,148,152,156,160-161,164-165,168-169,172-173,176-1"
	"77,180-181,184-185,188-189,192,196,200,204,208,212,216,220,224-225,228-229,232-233,236-237,240-241,244-245,248-2"
	"49,252-253,256,260,264,268,272,276,280,284,288-289,292-293,296-297,300-301,304-305,308-309,312-313,316-317,320,3"
	"24,328,332,336,340,344,348,352-353,356-357,360-361,364-365,368-369,372-373,376-377,380-381,384,388,392,396,400,4"
	"04,408,412,416-417,420-421,424-425,428-429,432-433,436-437,440-441,444-445,448,452,456,460,464,468,472,476,480-4"
	"81,484-485,488-489,492-493,496-497,500-501,504-505,508-509,512,516,520,524,528,532,536,540,544-545,548-549,552-5"
	"53,556-557,560-561,564-565,568-569,572-573,576,580,584,588,592,596,600,604,608-609,612-613,616-617,620-621,624-6"
	"25,628-629,632-633,636-637,640,644,648,652,656,660,664,668,672-673,676-677,680-681,684-685,688-689,692-693,696-6"
	"97,700-701,704,708,712,716,720,724,728,732,736-737,740-741,744-745,748-749,752-753,756-757,760-761,764-765,768,7"
	"72,776,780,784,788,792,796,800-801,804-805,808-809,812-813,816-817,820-821,824-825,828-829,832,836,840,844,848,8"
	"52,856,860,864-865,868-869,872-873,876-877,880-881,884-885,888-889,892-893,896,900,904,908,912,916,920,924,928-9"
	"29,932-933,936-937,940-941,944-945,948-949,952-953,956-957,960,964,968,972,976,980,984,988,992-993,996-997,1000-"
	"1001,1004-1005,1008-1009,1012-1013,1016-1017,1020-1021,1024,1028,1032,1036,1040,1044,1048,1052,1056-1057,1060-10"
	"61,1064-1065,1068-1069,1072-1073,1076-1077,1080-1081,1084-1085,1088,1092,1096,1100,1104,1108,1112,1116,1120-1121"
	",1124-1125,1128-1129,1132-1133,1136-1137,1140-1141,1144-1145,1148-1149,1152,1156,1160,1164,1168,1172,1176,1180,1"
	"184-1185,1188-1189,1192-1193,1196-1197,1200-1201,1204-1205,1208-1209,1212-1213,1216,1220,1224,1228,1232,1236,124"
	"0,1244,1248-1249,1252-1253,1256-1257,1260-1261,1264-1265,1268-1269,1272-1273,1276-1277,1280,1284,1288,1292,1296,"
	"1300,1304,1308,1312-1313,1316-1317,1320-1321,1324-1325,1328-1329,1332-1333,1336-1337,1340-1341,1344,1348,1352,13"
	"56,1360,1364,1368,1372,1376-1377,1380-1381,1384-1385,1388-1389,1392-1393,1396-1397,1400-1401,1404-1405,1408,1412"
	",1416,1420,1424,1428,1432,1436,1440-1441,1444-1445,1448-1449,1452-1453,1456-1457,1460-1461,1464-1465,1468-1469,1"
	"472,1476,1480,1484,1488,1492,1496,1500,1504-1505,1508-1509,1512-1513,1516-1517,1520-1521,1524-1525,1528-1529,153"
	"2-1533,1536,1540,1544,1548,1552,1556,1560,1564,1568-1569,1572-1573,1576-1577,1580-1581,1584-1585,1588-1589,1592-"
	"1593,1596-1597,1600,1604,1608,1612,1616,1620,1624,1628,1632-1633,1636-1637,1640-1641,1644-1645,1648-1649,1652-16"
	"53,1656-1657,1660-1661,1664,1668,1672,1676,1680,1684,1688,1692,1696-1697,1700-1701,1704-1705,1708-1709,1712-1713"
	",1716-1717,1720-1721,1724-1725,1728,1732,1736,1740,1744,1748,1752,1756,1760-1761,1764-1765,1768-1769,1772-1773,1"
	"776-1777,1780-1781,1784-1785,1788-1789,1792,1796,1800,1804,1808,1812,1816,1820,1824-1825,1828-1829,1832-1833,183"
	"6-1837,1840-1841,1844-1845,1848-1849,1852-1853,1856,1860,1864,1868,1872,1876,1880,1884,1888-1889,1892-1893,1896-"
	"1897,1900-1901,1904-1905,1908-1909,1912-1913,1916-1917,1920,1924,1928,1932,1936,1940,1944,1948,1952-1953,1956-19"
	"57,1960-1961,1964-1965,1968-1969,1972-1973,1976-1977,1980-1981,1984,1988,1992,1996,2000,2004,2008,2012,2016-2017"
	",2020-2021,2024-2025,2028-2029,2032-2033,2036-2037,2040-2041,2044-2045,2048,2052,2056,2060,2064,2068,2072,2076,2"
	"080-2081,2084-2085,2088-2089,2092-2093,2096-2097,2100-2101,2104-2105,2108-2109,2112,2116,2120,2124,2128,2132,213"
	"6,2140,2144-2145,2148-2149,2152-2153,2156-2157,2160-2161,2164-2165,2168-2169,2172-2173,2176,2180,2184,2188,2192,"
	"2196,2200,2204,2208-2209,2212-2213,2216-2217,2220-2221,2224-2225,2228-2229,2232-2233,2236-2237,2240,2244,2248,22"
	"52,2256,2260,2264,2268,2272-2273,2276-2277,2280-2281,2284-2285,2288-2289,2292-2293,2296-2297,2300-2301,2304,2308"
	",2312,2316,2320,2324,2328,2332,2336-2337,2340-2341,2344-2345,2348-2349,2352-2353,2356-2357,2360-2361,2364-2365,2"
	"368,2372,2376,2380,2384,2388,2392,2396,2400-2401,2404-2405,2408-2409,2412-2413,2416-2417,2420-2421,2424-2425,242"
	"8-2429,2432,2436,2440,2444,2448,2452,2456,2460,2464-2465,2468-2469,2472-2473,2476-2477,2480-2481,2484-2485,2488-"
	"2489,2492-2493,2496,2500,2504,2508,2512,2516,2520,2524,2528-2529,2532-2533,2536-2537,2540-2541,2544-2545,2548-25"
	"49,2552-2553,2556-2557\n";

static const struct test_bitmap_print test_print[] __initconst = {
	{ small_bitmap, sizeof(small_bitmap) * BITS_PER_BYTE, small_mask, small_list },
	{ large_bitmap, sizeof(large_bitmap) * BITS_PER_BYTE, large_mask, large_list },
};

static void __init test_bitmap_print_buf(void)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(test_print); i++) {
		const struct test_bitmap_print *t = &test_print[i];
		int n;

		n = bitmap_print_bitmask_to_buf(print_buf, t->bitmap, t->nbits,
						0, 2 * PAGE_SIZE);
		expect_eq_uint(strlen(t->mask) + 1, n);
		expect_eq_str(t->mask, print_buf, n);

		n = bitmap_print_list_to_buf(print_buf, t->bitmap, t->nbits,
					     0, 2 * PAGE_SIZE);
		expect_eq_uint(strlen(t->list) + 1, n);
		expect_eq_str(t->list, print_buf, n);

		/* test by non-zero offset */
		if (strlen(t->list) > PAGE_SIZE) {
			n = bitmap_print_list_to_buf(print_buf, t->bitmap, t->nbits,
						     PAGE_SIZE, PAGE_SIZE);
			expect_eq_uint(strlen(t->list) + 1 - PAGE_SIZE, n);
			expect_eq_str(t->list + PAGE_SIZE, print_buf, n);
		}
	}
}

/*
 * FIXME: Clang breaks compile-time evaluations when KASAN and GCOV are enabled.
 * To workaround it, GCOV is force-disabled in Makefile for this configuration.
 */
static void __init test_bitmap_const_eval(void)
{
	DECLARE_BITMAP(bitmap, BITS_PER_LONG);
	unsigned long initvar = BIT(2);
	unsigned long bitopvar = 0;
	unsigned long var = 0;
	int res;

	/*
	 * Compilers must be able to optimize all of those to compile-time
	 * constants on any supported optimization level (-O2, -Os) and any
	 * architecture. Otherwise, trigger a build bug.
	 * The whole function gets optimized out then, there's nothing to do
	 * in runtime.
	 */

	/* Equals to `unsigned long bitmap[1] = { GENMASK(6, 5), }` */
	bitmap_clear(bitmap, 0, BITS_PER_LONG);
	if (!test_bit(7, bitmap))
		bitmap_set(bitmap, 5, 2);

	/* Equals to `unsigned long bitopvar = BIT(20)` */
	__change_bit(31, &bitopvar);
	bitmap_shift_right(&bitopvar, &bitopvar, 11, BITS_PER_LONG);

	/* Equals to `unsigned long var = BIT(25)` */
	var |= BIT(25);
	if (var & BIT(0))
		var ^= GENMASK(9, 6);

	/* __const_hweight<32|64>(GENMASK(6, 5)) == 2 */
	res = bitmap_weight(bitmap, 20);
	BUILD_BUG_ON(!__builtin_constant_p(res));
	BUILD_BUG_ON(res != 2);

	/* !(BIT(31) & BIT(18)) == 1 */
	res = !test_bit(18, &bitopvar);
	BUILD_BUG_ON(!__builtin_constant_p(res));
	BUILD_BUG_ON(!res);

	/* BIT(2) & GENMASK(14, 8) == 0 */
	res = initvar & GENMASK(14, 8);
	BUILD_BUG_ON(!__builtin_constant_p(res));
	BUILD_BUG_ON(res);

	/* ~BIT(25) */
	BUILD_BUG_ON(!__builtin_constant_p(~var));
	BUILD_BUG_ON(~var != ~BIT(25));

	/* ~BIT(25) | BIT(25) == ~0UL */
	bitmap_complement(&var, &var, BITS_PER_LONG);
	__assign_bit(25, &var, true);

	/* !(~(~0UL)) == 1 */
	res = bitmap_full(&var, BITS_PER_LONG);
	BUILD_BUG_ON(!__builtin_constant_p(res));
	BUILD_BUG_ON(!res);
}

/*
 * Test bitmap should be big enough to include the cases when start is not in
 * the first word, and start+nbits lands in the following word.
 */
#define TEST_BIT_LEN (1000)

/*
 * Helper function to test bitmap_write() overwriting the chosen byte pattern.
 */
static void __init test_bitmap_write_helper(const char *pattern)
{
	DECLARE_BITMAP(bitmap, TEST_BIT_LEN);
	DECLARE_BITMAP(exp_bitmap, TEST_BIT_LEN);
	DECLARE_BITMAP(pat_bitmap, TEST_BIT_LEN);
	unsigned long w, r, bit;
	int i, n, nbits;

	/*
	 * Only parse the pattern once and store the result in the intermediate
	 * bitmap.
	 */
	bitmap_parselist(pattern, pat_bitmap, TEST_BIT_LEN);

	/*
	 * Check that writing a single bit does not accidentally touch the
	 * adjacent bits.
	 */
	for (i = 0; i < TEST_BIT_LEN; i++) {
		bitmap_copy(bitmap, pat_bitmap, TEST_BIT_LEN);
		bitmap_copy(exp_bitmap, pat_bitmap, TEST_BIT_LEN);
		for (bit = 0; bit <= 1; bit++) {
			bitmap_write(bitmap, bit, i, 1);
			__assign_bit(i, exp_bitmap, bit);
			expect_eq_bitmap(exp_bitmap, bitmap,
					 TEST_BIT_LEN);
		}
	}

	/* Ensure writing 0 bits does not change anything. */
	bitmap_copy(bitmap, pat_bitmap, TEST_BIT_LEN);
	bitmap_copy(exp_bitmap, pat_bitmap, TEST_BIT_LEN);
	for (i = 0; i < TEST_BIT_LEN; i++) {
		bitmap_write(bitmap, ~0UL, i, 0);
		expect_eq_bitmap(exp_bitmap, bitmap, TEST_BIT_LEN);
	}

	for (nbits = BITS_PER_LONG; nbits >= 1; nbits--) {
		w = IS_ENABLED(CONFIG_64BIT) ? 0xdeadbeefdeadbeefUL
					     : 0xdeadbeefUL;
		w >>= (BITS_PER_LONG - nbits);
		for (i = 0; i <= TEST_BIT_LEN - nbits; i++) {
			bitmap_copy(bitmap, pat_bitmap, TEST_BIT_LEN);
			bitmap_copy(exp_bitmap, pat_bitmap, TEST_BIT_LEN);
			for (n = 0; n < nbits; n++)
				__assign_bit(i + n, exp_bitmap, w & BIT(n));
			bitmap_write(bitmap, w, i, nbits);
			expect_eq_bitmap(exp_bitmap, bitmap, TEST_BIT_LEN);
			r = bitmap_read(bitmap, i, nbits);
			expect_eq_ulong(r, w);
		}
	}
}

static void __init test_bitmap_read_write(void)
{
	unsigned char *pattern[3] = {"", "all:1/2", "all"};
	DECLARE_BITMAP(bitmap, TEST_BIT_LEN);
	unsigned long zero_bits = 0, bits_per_long = BITS_PER_LONG;
	unsigned long val;
	int i, pi;

	/*
	 * Reading/writing zero bits should not crash the kernel.
	 * READ_ONCE() prevents constant folding.
	 */
	bitmap_write(NULL, 0, 0, READ_ONCE(zero_bits));
	/* Return value of bitmap_read() is undefined here. */
	bitmap_read(NULL, 0, READ_ONCE(zero_bits));

	/*
	 * Reading/writing more than BITS_PER_LONG bits should not crash the
	 * kernel. READ_ONCE() prevents constant folding.
	 */
	bitmap_write(NULL, 0, 0, READ_ONCE(bits_per_long) + 1);
	/* Return value of bitmap_read() is undefined here. */
	bitmap_read(NULL, 0, READ_ONCE(bits_per_long) + 1);

	/*
	 * Ensure that bitmap_read() reads the same value that was previously
	 * written, and two consequent values are correctly merged.
	 * The resulting bit pattern is asymmetric to rule out possible issues
	 * with bit numeration order.
	 */
	for (i = 0; i < TEST_BIT_LEN - 7; i++) {
		bitmap_zero(bitmap, TEST_BIT_LEN);

		bitmap_write(bitmap, 0b10101UL, i, 5);
		val = bitmap_read(bitmap, i, 5);
		expect_eq_ulong(0b10101UL, val);

		bitmap_write(bitmap, 0b101UL, i + 5, 3);
		val = bitmap_read(bitmap, i + 5, 3);
		expect_eq_ulong(0b101UL, val);

		val = bitmap_read(bitmap, i, 8);
		expect_eq_ulong(0b10110101UL, val);
	}

	for (pi = 0; pi < ARRAY_SIZE(pattern); pi++)
		test_bitmap_write_helper(pattern[pi]);
}

static void __init test_bitmap_read_perf(void)
{
	DECLARE_BITMAP(bitmap, TEST_BIT_LEN);
	unsigned int cnt, nbits, i;
	unsigned long val;
	ktime_t time;

	bitmap_fill(bitmap, TEST_BIT_LEN);
	time = ktime_get();
	for (cnt = 0; cnt < 5; cnt++) {
		for (nbits = 1; nbits <= BITS_PER_LONG; nbits++) {
			for (i = 0; i < TEST_BIT_LEN; i++) {
				if (i + nbits > TEST_BIT_LEN)
					break;
				/*
				 * Prevent the compiler from optimizing away the
				 * bitmap_read() by using its value.
				 */
				WRITE_ONCE(val, bitmap_read(bitmap, i, nbits));
			}
		}
	}
	time = ktime_get() - time;
	pr_info("Time spent in %s:\t%llu\n", __func__, time);
}

static void __init test_bitmap_write_perf(void)
{
	DECLARE_BITMAP(bitmap, TEST_BIT_LEN);
	unsigned int cnt, nbits, i;
	unsigned long val = 0xfeedface;
	ktime_t time;

	bitmap_zero(bitmap, TEST_BIT_LEN);
	time = ktime_get();
	for (cnt = 0; cnt < 5; cnt++) {
		for (nbits = 1; nbits <= BITS_PER_LONG; nbits++) {
			for (i = 0; i < TEST_BIT_LEN; i++) {
				if (i + nbits > TEST_BIT_LEN)
					break;
				bitmap_write(bitmap, val, i, nbits);
			}
		}
	}
	time = ktime_get() - time;
	pr_info("Time spent in %s:\t%llu\n", __func__, time);
}

#undef TEST_BIT_LEN

static void __init selftest(void)
{
	test_zero_clear();
	test_fill_set();
	test_copy();
	test_bitmap_region();
	test_replace();
	test_bitmap_sg();
	test_bitmap_arr32();
	test_bitmap_arr64();
	test_bitmap_parse();
	test_bitmap_parselist();
	test_bitmap_printlist();
	test_mem_optimisations();
	test_bitmap_cut();
	test_bitmap_print_buf();
	test_bitmap_const_eval();
	test_bitmap_read_write();
	test_bitmap_read_perf();
	test_bitmap_write_perf();

	test_find_nth_bit();
	test_for_each_set_bit();
	test_for_each_set_bit_from();
	test_for_each_clear_bit();
	test_for_each_clear_bit_from();
	test_for_each_set_bitrange();
	test_for_each_clear_bitrange();
	test_for_each_set_bitrange_from();
	test_for_each_clear_bitrange_from();
	test_for_each_set_clump8();
	test_for_each_set_bit_wrap();
}

KSTM_MODULE_LOADERS(test_bitmap);
MODULE_AUTHOR("david decotigny <david.decotigny@googlers.com>");
MODULE_LICENSE("GPL");
