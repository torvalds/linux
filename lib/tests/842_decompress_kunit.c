// SPDX-License-Identifier: GPL-2.0-or-later

#include <kunit/test.h>
#include <linux/sw842.h>

#define SW842_GUARD_SIZE	8
#define SW842_MAX_OUTPUT	16
#define SW842_GUARD_BYTE	0x42
#define SW842_OUTPUT_POISON	0x5a

/* ZEROS, I8(0), END, CRC32. */
static const u8 sw842_index_exact_fit[] = {
	0xe6, 0x40, 0x3c, 0x00, 0x00, 0x00, 0x00,
};

/* D8(eight zero bytes), I8(0), END, CRC32. */
static const u8 sw842_index_output_overflow[] = {
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x06, 0x40, 0x3c, 0x00, 0x00, 0x00, 0x00,
};

/* ZEROS, SHORT_DATA(01 02 03 04 05), END, CRC32. */
static const u8 sw842_short_data_exact_fit[] = {
	0xe7, 0x68, 0x08, 0x10, 0x18, 0x20,
	0x2f, 0xa9, 0x67, 0xfc, 0x07, 0xc0,
};

/* ZEROS, SHORT_DATA(five zero bytes), END, CRC32. */
static const u8 sw842_short_data_output_overflow[] = {
	0xe7, 0x48, 0x07, 0x80, 0x00, 0x00, 0x00, 0x00,
};

/* ZEROS, REPEAT(one block), END, CRC32. */
static const u8 sw842_repeat_exact_history[] = {
	0xe6, 0xc0, 0xf0, 0x00, 0x00, 0x00, 0x00,
};

/*
 * SHORT_DATA(seven zero bytes), REPEAT(one block), END, CRC32.
 * CRC32 includes SW842_GUARD_BYTE copied from before output into byte 7.
 */
static const u8 sw842_repeat_without_full_history[] = {
	0xef, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0xd8, 0x1e, 0x09, 0xa3, 0x1d, 0xd6,
};

static const u8 sw842_zero_output[SW842_MAX_OUTPUT];
static const u8 sw842_short_data_output[] = {
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x01, 0x02, 0x03, 0x04, 0x05,
};

struct sw842_decompress_test_case {
	const char *name;
	const u8 *compressed;
	unsigned int compressed_len;
	const u8 *expected_output;
	unsigned int output_capacity;
	int expected_ret;
};

#define SW842_DECOMPRESS_CASE(_name, _compressed, _compressed_len, _expected, \
			      _capacity, _ret) \
	{ \
		.name = _name, \
		.compressed = _compressed, \
		.compressed_len = _compressed_len, \
		.expected_output = _expected, \
		.output_capacity = _capacity, \
		.expected_ret = _ret, \
	}

static const struct sw842_decompress_test_case sw842_decompress_cases[] = {
	SW842_DECOMPRESS_CASE("index_exact_fit", sw842_index_exact_fit,
			      ARRAY_SIZE(sw842_index_exact_fit),
			      sw842_zero_output, 16, 0),
	SW842_DECOMPRESS_CASE("index_output_overflow",
			      sw842_index_output_overflow,
			      ARRAY_SIZE(sw842_index_output_overflow),
			      sw842_zero_output, 8, -ENOSPC),
	SW842_DECOMPRESS_CASE("short_data_exact_fit",
			      sw842_short_data_exact_fit,
			      ARRAY_SIZE(sw842_short_data_exact_fit),
			      sw842_short_data_output,
			      sizeof(sw842_short_data_output), 0),
	SW842_DECOMPRESS_CASE("short_data_output_overflow",
			      sw842_short_data_output_overflow,
			      ARRAY_SIZE(sw842_short_data_output_overflow),
			      sw842_zero_output, 8, -ENOSPC),
	SW842_DECOMPRESS_CASE("repeat_exact_history",
			      sw842_repeat_exact_history,
			      ARRAY_SIZE(sw842_repeat_exact_history),
			      sw842_zero_output, 16, 0),
	SW842_DECOMPRESS_CASE("repeat_without_full_history",
			      sw842_repeat_without_full_history,
			      ARRAY_SIZE(sw842_repeat_without_full_history),
			      sw842_zero_output, 15, -EINVAL),
};

KUNIT_ARRAY_PARAM_DESC(sw842_decompress, sw842_decompress_cases, name);

static void sw842_decompress_test(struct kunit *test)
{
	const struct sw842_decompress_test_case *test_case = test->param_value;
	u8 storage[SW842_GUARD_SIZE + SW842_MAX_OUTPUT + SW842_GUARD_SIZE];
	u8 expected_guard[SW842_GUARD_SIZE];
	u8 *output = storage + SW842_GUARD_SIZE;
	unsigned int output_len = test_case->output_capacity;
	int ret;

	KUNIT_ASSERT_LE(test, test_case->output_capacity, SW842_MAX_OUTPUT);
	memset(storage, SW842_GUARD_BYTE, sizeof(storage));
	memset(expected_guard, SW842_GUARD_BYTE, sizeof(expected_guard));
	memset(output, SW842_OUTPUT_POISON, test_case->output_capacity);

	ret = sw842_decompress(test_case->compressed,
			       test_case->compressed_len, output, &output_len);

	KUNIT_EXPECT_EQ(test, ret, test_case->expected_ret);
	/* Every successful case is an exact-fit boundary test. */
	KUNIT_EXPECT_EQ(test, output_len,
			test_case->expected_ret ? 0U : test_case->output_capacity);
	if (!test_case->expected_ret)
		KUNIT_EXPECT_MEMEQ(test, output, test_case->expected_output,
				   test_case->output_capacity);
	KUNIT_EXPECT_MEMEQ(test, storage, expected_guard, SW842_GUARD_SIZE);
	KUNIT_EXPECT_MEMEQ(test, output + test_case->output_capacity,
			   expected_guard, SW842_GUARD_SIZE);
}

static struct kunit_case sw842_decompress_test_cases[] = {
	KUNIT_CASE_PARAM(sw842_decompress_test, sw842_decompress_gen_params),
	{}
};

static struct kunit_suite sw842_decompress_test_suite = {
	.name = "842-decompress",
	.test_cases = sw842_decompress_test_cases,
};

kunit_test_suite(sw842_decompress_test_suite);

MODULE_DESCRIPTION("Software 842 decompressor KUnit tests");
MODULE_LICENSE("GPL");
