// SPDX-License-Identifier: GPL-2.0+
/*
 * Test cases for bitfield helpers.
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <kunit/test.h>
#include <linux/bitfield.h>

#define CHECK_ENC_GET_U(tp, v, field, res) do {				\
		{							\
			u##tp _res;					\
									\
			_res = u##tp##_encode_bits(v, field);		\
			KUNIT_ASSERT_FALSE_MSG(context, _res != res,	\
				       "u" #tp "_encode_bits(" #v ", " #field ") is 0x%llx != " #res "\n",	\
				       (u64)_res);			\
			KUNIT_ASSERT_FALSE(context,			\
				   u##tp##_get_bits(_res, field) != v);	\
		}							\
	} while (0)

#define CHECK_ENC_GET_LE(tp, v, field, res) do {			\
		{							\
			__le##tp _res;					\
									\
			_res = le##tp##_encode_bits(v, field);		\
			KUNIT_ASSERT_FALSE_MSG(context,			\
				       _res != cpu_to_le##tp(res),	\
				       "le" #tp "_encode_bits(" #v ", " #field ") is 0x%llx != 0x%llx",\
				       (u64)le##tp##_to_cpu(_res),	\
				       (u64)(res));			\
			KUNIT_ASSERT_FALSE(context,			\
				   le##tp##_get_bits(_res, field) != v);\
		}							\
	} while (0)

#define CHECK_ENC_GET_BE(tp, v, field, res) do {			\
		{							\
			__be##tp _res;					\
									\
			_res = be##tp##_encode_bits(v, field);		\
			KUNIT_ASSERT_FALSE_MSG(context,			\
				       _res != cpu_to_be##tp(res),	\
				       "be" #tp "_encode_bits(" #v ", " #field ") is 0x%llx != 0x%llx",	\
				       (u64)be##tp##_to_cpu(_res),	\
				       (u64)(res));			\
			KUNIT_ASSERT_FALSE(context,			\
				   be##tp##_get_bits(_res, field) != v);\
		}							\
	} while (0)

#define CHECK_ENC_GET(tp, v, field, res) do {				\
		CHECK_ENC_GET_U(tp, v, field, res);			\
		CHECK_ENC_GET_LE(tp, v, field, res);			\
		CHECK_ENC_GET_BE(tp, v, field, res);			\
	} while (0)

static void __init test_bitfields_constants(struct kunit *context)
{
	/*
	 * NOTE
	 * This whole function compiles (or at least should, if everything
	 * is going according to plan) to nothing after optimisation.
	 */

	CHECK_ENC_GET(16,  1, 0x000f, 0x0001);
	CHECK_ENC_GET(16,  3, 0x00f0, 0x0030);
	CHECK_ENC_GET(16,  5, 0x0f00, 0x0500);
	CHECK_ENC_GET(16,  7, 0xf000, 0x7000);
	CHECK_ENC_GET(16, 14, 0x000f, 0x000e);
	CHECK_ENC_GET(16, 15, 0x00f0, 0x00f0);

	CHECK_ENC_GET_U(8,  1, 0x0f, 0x01);
	CHECK_ENC_GET_U(8,  3, 0xf0, 0x30);
	CHECK_ENC_GET_U(8, 14, 0x0f, 0x0e);
	CHECK_ENC_GET_U(8, 15, 0xf0, 0xf0);

	CHECK_ENC_GET(32,  1, 0x00000f00, 0x00000100);
	CHECK_ENC_GET(32,  3, 0x0000f000, 0x00003000);
	CHECK_ENC_GET(32,  5, 0x000f0000, 0x00050000);
	CHECK_ENC_GET(32,  7, 0x00f00000, 0x00700000);
	CHECK_ENC_GET(32, 14, 0x0f000000, 0x0e000000);
	CHECK_ENC_GET(32, 15, 0xf0000000, 0xf0000000);

	CHECK_ENC_GET(64,  1, 0x00000f0000000000ull, 0x0000010000000000ull);
	CHECK_ENC_GET(64,  3, 0x0000f00000000000ull, 0x0000300000000000ull);
	CHECK_ENC_GET(64,  5, 0x000f000000000000ull, 0x0005000000000000ull);
	CHECK_ENC_GET(64,  7, 0x00f0000000000000ull, 0x0070000000000000ull);
	CHECK_ENC_GET(64, 14, 0x0f00000000000000ull, 0x0e00000000000000ull);
	CHECK_ENC_GET(64, 15, 0xf000000000000000ull, 0xf000000000000000ull);
}

#define CHECK(tp, mask) do {						\
		u64 v;							\
									\
		for (v = 0; v < 1 << hweight32(mask); v++)		\
			KUNIT_ASSERT_FALSE(context,			\
				tp##_encode_bits(v, mask) != v << __ffs64(mask));\
	} while (0)

static void __init test_bitfields_variables(struct kunit *context)
{
	CHECK(u8, 0x0f);
	CHECK(u8, 0xf0);
	CHECK(u8, 0x38);

	CHECK(u16, 0x0038);
	CHECK(u16, 0x0380);
	CHECK(u16, 0x3800);
	CHECK(u16, 0x8000);

	CHECK(u32, 0x80000000);
	CHECK(u32, 0x7f000000);
	CHECK(u32, 0x07e00000);
	CHECK(u32, 0x00018000);

	CHECK(u64, 0x8000000000000000ull);
	CHECK(u64, 0x7f00000000000000ull);
	CHECK(u64, 0x0001800000000000ull);
	CHECK(u64, 0x0000000080000000ull);
	CHECK(u64, 0x000000007f000000ull);
	CHECK(u64, 0x0000000018000000ull);
	CHECK(u64, 0x0000001f8000000ull);
}

#ifdef TEST_BITFIELD_COMPILE
static void __init test_bitfields_compile(struct kunit *context)
{
	/* these should fail compilation */
	CHECK_ENC_GET(16, 16, 0x0f00, 0x1000);
	u32_encode_bits(7, 0x06000000);

	/* this should at least give a warning */
	u16_encode_bits(0, 0x60000);
}
#endif

static struct kunit_case __refdata bitfields_test_cases[] = {
	KUNIT_CASE(test_bitfields_constants),
	KUNIT_CASE(test_bitfields_variables),
	{}
};

static struct kunit_suite bitfields_test_suite = {
	.name = "bitfields",
	.test_cases = bitfields_test_cases,
};

kunit_test_suites(&bitfields_test_suite);

MODULE_AUTHOR("Johannes Berg <johannes@sipsolutions.net>");
MODULE_LICENSE("GPL");
