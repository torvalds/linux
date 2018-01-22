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
		pr_warn("[%s:%u] expected %u, got %u\n",
			srcfile, line, exp_uint, x);
		return false;
	}
	return true;
}


static bool __init
__check_eq_bitmap(const char *srcfile, unsigned int line,
		  const unsigned long *exp_bmap, unsigned int exp_nbits,
		  const unsigned long *bmap, unsigned int nbits)
{
	if (exp_nbits != nbits) {
		pr_warn("[%s:%u] bitmap length mismatch: expected %u, got %u\n",
			srcfile, line, exp_nbits, nbits);
		return false;
	}

	if (!bitmap_equal(exp_bmap, bmap, nbits)) {
		pr_warn("[%s:%u] bitmaps contents differ: expected \"%*pbl\", got \"%*pbl\"\n",
			srcfile, line,
			exp_nbits, exp_bmap, nbits, bmap);
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

static void __init test_zero_fill_copy(void)
{
	DECLARE_BITMAP(bmap1, 1024);
	DECLARE_BITMAP(bmap2, 1024);

	bitmap_zero(bmap1, 1024);
	bitmap_zero(bmap2, 1024);

	/* single-word bitmaps */
	expect_eq_pbl("", bmap1, 23);

	bitmap_fill(bmap1, 19);
	expect_eq_pbl("0-18", bmap1, 1024);

	bitmap_copy(bmap2, bmap1, 23);
	expect_eq_pbl("0-18", bmap2, 1024);

	bitmap_fill(bmap2, 23);
	expect_eq_pbl("0-22", bmap2, 1024);

	bitmap_copy(bmap2, bmap1, 23);
	expect_eq_pbl("0-18", bmap2, 1024);

	bitmap_zero(bmap1, 23);
	expect_eq_pbl("", bmap1, 1024);

	/* multi-word bitmaps */
	bitmap_zero(bmap1, 1024);
	expect_eq_pbl("", bmap1, 1024);

	bitmap_fill(bmap1, 109);
	expect_eq_pbl("0-108", bmap1, 1024);

	bitmap_copy(bmap2, bmap1, 1024);
	expect_eq_pbl("0-108", bmap2, 1024);

	bitmap_fill(bmap2, 1024);
	expect_eq_pbl("0-1023", bmap2, 1024);

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

	bitmap_zero(bmap2, 97);  /* ... but 0-padded til word length */
	expect_eq_pbl("128-1023", bmap2, 1024);
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

static void __init test_bitmap_u32_array_conversions(void)
{
	DECLARE_BITMAP(bmap1, 1024);
	DECLARE_BITMAP(bmap2, 1024);
	u32 exp_arr[32], arr[32];
	unsigned nbits;

	for (nbits = 0 ; nbits < 257 ; ++nbits) {
		const unsigned int used_u32s = DIV_ROUND_UP(nbits, 32);
		unsigned int i, rv;

		bitmap_zero(bmap1, nbits);
		bitmap_set(bmap1, nbits, 1024 - nbits);  /* garbage */

		memset(arr, 0xff, sizeof(arr));
		rv = bitmap_to_u32array(arr, used_u32s, bmap1, nbits);
		expect_eq_uint(nbits, rv);

		memset(exp_arr, 0xff, sizeof(exp_arr));
		memset(exp_arr, 0, used_u32s*sizeof(*exp_arr));
		expect_eq_u32_array(exp_arr, 32, arr, 32);

		bitmap_fill(bmap2, 1024);
		rv = bitmap_from_u32array(bmap2, nbits, arr, used_u32s);
		expect_eq_uint(nbits, rv);
		expect_eq_bitmap(bmap1, 1024, bmap2, 1024);

		for (i = 0 ; i < nbits ; ++i) {
			/*
			 * test conversion bitmap -> u32[]
			 */

			bitmap_zero(bmap1, 1024);
			__set_bit(i, bmap1);
			bitmap_set(bmap1, nbits, 1024 - nbits);  /* garbage */

			memset(arr, 0xff, sizeof(arr));
			rv = bitmap_to_u32array(arr, used_u32s, bmap1, nbits);
			expect_eq_uint(nbits, rv);

			/* 1st used u32 words contain expected bit set, the
			 * remaining words are left unchanged (0xff)
			 */
			memset(exp_arr, 0xff, sizeof(exp_arr));
			memset(exp_arr, 0, used_u32s*sizeof(*exp_arr));
			exp_arr[i/32] = (1U<<(i%32));
			expect_eq_u32_array(exp_arr, 32, arr, 32);


			/* same, with longer array to fill
			 */
			memset(arr, 0xff, sizeof(arr));
			rv = bitmap_to_u32array(arr, 32, bmap1, nbits);
			expect_eq_uint(nbits, rv);

			/* 1st used u32 words contain expected bit set, the
			 * remaining words are all 0s
			 */
			memset(exp_arr, 0, sizeof(exp_arr));
			exp_arr[i/32] = (1U<<(i%32));
			expect_eq_u32_array(exp_arr, 32, arr, 32);

			/*
			 * test conversion u32[] -> bitmap
			 */

			/* the 1st nbits of bmap2 are identical to
			 * bmap1, the remaining bits of bmap2 are left
			 * unchanged (all 1s)
			 */
			bitmap_fill(bmap2, 1024);
			rv = bitmap_from_u32array(bmap2, nbits,
						  exp_arr, used_u32s);
			expect_eq_uint(nbits, rv);

			expect_eq_bitmap(bmap1, 1024, bmap2, 1024);

			/* same, with more bits to fill
			 */
			memset(arr, 0xff, sizeof(arr));  /* garbage */
			memset(arr, 0, used_u32s*sizeof(u32));
			arr[i/32] = (1U<<(i%32));

			bitmap_fill(bmap2, 1024);
			rv = bitmap_from_u32array(bmap2, 1024, arr, used_u32s);
			expect_eq_uint(used_u32s*32, rv);

			/* the 1st nbits of bmap2 are identical to
			 * bmap1, the remaining bits of bmap2 are cleared
			 */
			bitmap_zero(bmap1, 1024);
			__set_bit(i, bmap1);
			expect_eq_bitmap(bmap1, 1024, bmap2, 1024);


			/*
			 * test short conversion bitmap -> u32[] (1
			 * word too short)
			 */
			if (used_u32s > 1) {
				bitmap_zero(bmap1, 1024);
				__set_bit(i, bmap1);
				bitmap_set(bmap1, nbits,
					   1024 - nbits);  /* garbage */
				memset(arr, 0xff, sizeof(arr));

				rv = bitmap_to_u32array(arr, used_u32s - 1,
							bmap1, nbits);
				expect_eq_uint((used_u32s - 1)*32, rv);

				/* 1st used u32 words contain expected
				 * bit set, the remaining words are
				 * left unchanged (0xff)
				 */
				memset(exp_arr, 0xff, sizeof(exp_arr));
				memset(exp_arr, 0,
				       (used_u32s-1)*sizeof(*exp_arr));
				if ((i/32) < (used_u32s - 1))
					exp_arr[i/32] = (1U<<(i%32));
				expect_eq_u32_array(exp_arr, 32, arr, 32);
			}

			/*
			 * test short conversion u32[] -> bitmap (3
			 * bits too short)
			 */
			if (nbits > 3) {
				memset(arr, 0xff, sizeof(arr));  /* garbage */
				memset(arr, 0, used_u32s*sizeof(*arr));
				arr[i/32] = (1U<<(i%32));

				bitmap_zero(bmap1, 1024);
				rv = bitmap_from_u32array(bmap1, nbits - 3,
							  arr, used_u32s);
				expect_eq_uint(nbits - 3, rv);

				/* we are expecting the bit < nbits -
				 * 3 (none otherwise), and the rest of
				 * bmap1 unchanged (0-filled)
				 */
				bitmap_zero(bmap2, 1024);
				if (i < nbits - 3)
					__set_bit(i, bmap2);
				expect_eq_bitmap(bmap2, 1024, bmap1, 1024);

				/* do the same with bmap1 initially
				 * 1-filled
				 */

				bitmap_fill(bmap1, 1024);
				rv = bitmap_from_u32array(bmap1, nbits - 3,
							 arr, used_u32s);
				expect_eq_uint(nbits - 3, rv);

				/* we are expecting the bit < nbits -
				 * 3 (none otherwise), and the rest of
				 * bmap1 unchanged (1-filled)
				 */
				bitmap_zero(bmap2, 1024);
				if (i < nbits - 3)
					__set_bit(i, bmap2);
				bitmap_set(bmap2, nbits-3, 1024 - nbits + 3);
				expect_eq_bitmap(bmap2, 1024, bmap1, 1024);
			}
		}
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
	test_zero_fill_copy();
	test_bitmap_u32_array_conversions();
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
