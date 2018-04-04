/*
 * Test cases for printf facility.
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/bitmap.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/printk.h>
#include <linux/slab.h>
#include <linux/string.h>

static unsigned total_tests __initdata;
static unsigned failed_tests __initdata;

static char pbl_buffer[PAGE_SIZE] __initdata;


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

#define PARSE_TIME 0x1

struct test_bitmap_parselist{
	const int errno;
	const char *in;
	const unsigned long *expected;
	const int nbits;
	const int flags;
};

static const unsigned long exp[] __initconst = {
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
	BITMAP_FROM_U64(0xffffffff77777777ULL)
};

static const unsigned long exp2[] __initconst = {
	BITMAP_FROM_U64(0x3333333311111111ULL),
	BITMAP_FROM_U64(0xffffffff77777777ULL)
};

static const struct test_bitmap_parselist parselist_tests[] __initconst = {
#define step (sizeof(u64) / sizeof(unsigned long))

	{0, "0",			&exp[0], 8, 0},
	{0, "1",			&exp[1 * step], 8, 0},
	{0, "0-15",			&exp[2 * step], 32, 0},
	{0, "16-31",			&exp[3 * step], 32, 0},
	{0, "0-31:1/2",			&exp[4 * step], 32, 0},
	{0, "1-31:1/2",			&exp[5 * step], 32, 0},
	{0, "0-31:1/4",			&exp[6 * step], 32, 0},
	{0, "1-31:1/4",			&exp[7 * step], 32, 0},
	{0, "0-31:4/4",			&exp[8 * step], 32, 0},
	{0, "1-31:4/4",			&exp[9 * step], 32, 0},
	{0, "0-31:1/4,32-63:2/4",	&exp[10 * step], 64, 0},
	{0, "0-31:3/4,32-63:4/4",	&exp[11 * step], 64, 0},

	{0, "0-31:1/4,32-63:2/4,64-95:3/4,96-127:4/4",	exp2, 128, 0},

	{0, "0-2047:128/256", NULL, 2048, PARSE_TIME},

	{-EINVAL, "-1",	NULL, 8, 0},
	{-EINVAL, "-0",	NULL, 8, 0},
	{-EINVAL, "10-1", NULL, 8, 0},
	{-EINVAL, "0-31:10/1", NULL, 8, 0},
};

static void __init test_bitmap_parselist(void)
{
	int i;
	int err;
	cycles_t cycles;
	DECLARE_BITMAP(bmap, 2048);

	for (i = 0; i < ARRAY_SIZE(parselist_tests); i++) {
#define ptest parselist_tests[i]

		cycles = get_cycles();
		err = bitmap_parselist(ptest.in, bmap, ptest.nbits);
		cycles = get_cycles() - cycles;

		if (err != ptest.errno) {
			pr_err("test %d: input is %s, errno is %d, expected %d\n",
					i, ptest.in, err, ptest.errno);
			continue;
		}

		if (!err && ptest.expected
			 && !__bitmap_equal(bmap, ptest.expected, ptest.nbits)) {
			pr_err("test %d: input is %s, result is 0x%lx, expected 0x%lx\n",
					i, ptest.in, bmap[0], *ptest.expected);
			continue;
		}

		if (ptest.flags & PARSE_TIME)
			pr_err("test %d: input is '%s' OK, Time: %llu\n",
					i, ptest.in,
					(unsigned long long)cycles);
	}
}

static void __init test_bitmap_arr32(void)
{
	unsigned int nbits, next_bit, len = sizeof(exp) * 8;
	u32 arr[sizeof(exp) / 4];
	DECLARE_BITMAP(bmap2, len);

	memset(arr, 0xa5, sizeof(arr));

	for (nbits = 0; nbits < len; ++nbits) {
		bitmap_to_arr32(arr, exp, nbits);
		bitmap_from_arr32(bmap2, arr, nbits);
		expect_eq_bitmap(bmap2, exp, nbits);

		next_bit = find_next_bit(bmap2,
				round_up(nbits, BITS_PER_LONG), nbits);
		if (next_bit < round_up(nbits, BITS_PER_LONG))
			pr_err("bitmap_copy_arr32(nbits == %d:"
				" tail is not safely cleared: %d\n",
				nbits, next_bit);

		if (nbits < len - 32)
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
		memset(bmap1, 0x5a, sizeof(bmap1));
		memset(bmap2, 0x5a, sizeof(bmap2));
		for (nbits = 0; nbits < 1024 - start; nbits += 8) {
			bitmap_set(bmap1, start, nbits);
			__bitmap_set(bmap2, start, nbits);
			if (!bitmap_equal(bmap1, bmap2, 1024))
				printk("set not equal %d %d\n", start, nbits);
			if (!__bitmap_equal(bmap1, bmap2, 1024))
				printk("set not __equal %d %d\n", start, nbits);

			bitmap_clear(bmap1, start, nbits);
			__bitmap_clear(bmap2, start, nbits);
			if (!bitmap_equal(bmap1, bmap2, 1024))
				printk("clear not equal %d %d\n", start, nbits);
			if (!__bitmap_equal(bmap1, bmap2, 1024))
				printk("clear not __equal %d %d\n", start,
									nbits);
		}
	}
}

static int __init test_bitmap_init(void)
{
	test_zero_clear();
	test_fill_set();
	test_copy();
	test_bitmap_arr32();
	test_bitmap_parselist();
	test_mem_optimisations();

	if (failed_tests == 0)
		pr_info("all %u tests passed\n", total_tests);
	else
		pr_warn("failed %u out of %u tests\n",
			failed_tests, total_tests);

	return failed_tests ? -EINVAL : 0;
}

static void __exit test_bitmap_cleanup(void)
{
}

module_init(test_bitmap_init);
module_exit(test_bitmap_cleanup);

MODULE_AUTHOR("david decotigny <david.decotigny@googlers.com>");
MODULE_LICENSE("GPL");
