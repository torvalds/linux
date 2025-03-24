// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2024, Vladimir Oltean <olteanv@gmail.com>
 * Copyright (c) 2024, Intel Corporation.
 */
#include <kunit/test.h>
#include <linux/packing.h>

struct packing_test_case {
	const char *desc;
	const u8 *pbuf;
	size_t pbuf_size;
	u64 uval;
	size_t start_bit;
	size_t end_bit;
	u8 quirks;
};

#define NO_QUIRKS	0

/**
 * PBUF - Initialize .pbuf and .pbuf_size
 * @array: elements of constant physical buffer
 *
 * Initializes the .pbuf and .pbuf_size fields of a struct packing_test_case
 * with a constant array of the specified elements.
 */
#define PBUF(array...)					\
	.pbuf = (const u8[]){ array },			\
	.pbuf_size = sizeof((const u8 []){ array })

static const struct packing_test_case cases[] = {
	/* These tests pack and unpack a magic 64-bit value
	 * (0xcafedeadbeefcafe) at a fixed logical offset (32) within an
	 * otherwise zero array of 128 bits (16 bytes). They test all possible
	 * bit layouts of the 128 bit buffer.
	 */
	{
		.desc = "no quirks, 16 bytes",
		PBUF(0x00, 0x00, 0x00, 0x00, 0xca, 0xfe, 0xde, 0xad,
		     0xbe, 0xef, 0xca, 0xfe, 0x00, 0x00, 0x00, 0x00),
		.uval = 0xcafedeadbeefcafe,
		.start_bit = 95,
		.end_bit = 32,
		.quirks = NO_QUIRKS,
	},
	{
		.desc = "lsw32 first, 16 bytes",
		PBUF(0x00, 0x00, 0x00, 0x00, 0xbe, 0xef, 0xca, 0xfe,
		     0xca, 0xfe, 0xde, 0xad, 0x00, 0x00, 0x00, 0x00),
		.uval = 0xcafedeadbeefcafe,
		.start_bit = 95,
		.end_bit = 32,
		.quirks = QUIRK_LSW32_IS_FIRST,
	},
	{
		.desc = "little endian words, 16 bytes",
		PBUF(0x00, 0x00, 0x00, 0x00, 0xad, 0xde, 0xfe, 0xca,
		     0xfe, 0xca, 0xef, 0xbe, 0x00, 0x00, 0x00, 0x00),
		.uval = 0xcafedeadbeefcafe,
		.start_bit = 95,
		.end_bit = 32,
		.quirks = QUIRK_LITTLE_ENDIAN,
	},
	{
		.desc = "lsw32 first + little endian words, 16 bytes",
		PBUF(0x00, 0x00, 0x00, 0x00, 0xfe, 0xca, 0xef, 0xbe,
		     0xad, 0xde, 0xfe, 0xca, 0x00, 0x00, 0x00, 0x00),
		.uval = 0xcafedeadbeefcafe,
		.start_bit = 95,
		.end_bit = 32,
		.quirks = QUIRK_LSW32_IS_FIRST | QUIRK_LITTLE_ENDIAN,
	},
	{
		.desc = "msb right, 16 bytes",
		PBUF(0x00, 0x00, 0x00, 0x00, 0x53, 0x7f, 0x7b, 0xb5,
		     0x7d, 0xf7, 0x53, 0x7f, 0x00, 0x00, 0x00, 0x00),
		.uval = 0xcafedeadbeefcafe,
		.start_bit = 95,
		.end_bit = 32,
		.quirks = QUIRK_MSB_ON_THE_RIGHT,
	},
	{
		.desc = "msb right + lsw32 first, 16 bytes",
		PBUF(0x00, 0x00, 0x00, 0x00, 0x7d, 0xf7, 0x53, 0x7f,
		     0x53, 0x7f, 0x7b, 0xb5, 0x00, 0x00, 0x00, 0x00),
		.uval = 0xcafedeadbeefcafe,
		.start_bit = 95,
		.end_bit = 32,
		.quirks = QUIRK_MSB_ON_THE_RIGHT | QUIRK_LSW32_IS_FIRST,
	},
	{
		.desc = "msb right + little endian words, 16 bytes",
		PBUF(0x00, 0x00, 0x00, 0x00, 0xb5, 0x7b, 0x7f, 0x53,
		     0x7f, 0x53, 0xf7, 0x7d, 0x00, 0x00, 0x00, 0x00),
		.uval = 0xcafedeadbeefcafe,
		.start_bit = 95,
		.end_bit = 32,
		.quirks = QUIRK_MSB_ON_THE_RIGHT | QUIRK_LITTLE_ENDIAN,
	},
	{
		.desc = "msb right + lsw32 first + little endian words, 16 bytes",
		PBUF(0x00, 0x00, 0x00, 0x00, 0x7f, 0x53, 0xf7, 0x7d,
		     0xb5, 0x7b, 0x7f, 0x53, 0x00, 0x00, 0x00, 0x00),
		.uval = 0xcafedeadbeefcafe,
		.start_bit = 95,
		.end_bit = 32,
		.quirks = QUIRK_MSB_ON_THE_RIGHT | QUIRK_LSW32_IS_FIRST | QUIRK_LITTLE_ENDIAN,
	},
	/* These tests pack and unpack a magic 64-bit value
	 * (0xcafedeadbeefcafe) at a fixed logical offset (32) within an
	 * otherwise zero array of varying size from 18 bytes to 24 bytes.
	 */
	{
		.desc = "no quirks, 18 bytes",
		PBUF(0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xca, 0xfe,
		     0xde, 0xad, 0xbe, 0xef, 0xca, 0xfe, 0x00, 0x00,
		     0x00, 0x00),
		.uval = 0xcafedeadbeefcafe,
		.start_bit = 95,
		.end_bit = 32,
		.quirks = NO_QUIRKS,
	},
	{
		.desc = "no quirks, 19 bytes",
		PBUF(0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xca,
		     0xfe, 0xde, 0xad, 0xbe, 0xef, 0xca, 0xfe, 0x00,
		     0x00, 0x00, 0x00),
		.uval = 0xcafedeadbeefcafe,
		.start_bit = 95,
		.end_bit = 32,
		.quirks = NO_QUIRKS,
	},
	{
		.desc = "no quirks, 20 bytes",
		PBUF(0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
		     0xca, 0xfe, 0xde, 0xad, 0xbe, 0xef, 0xca, 0xfe,
		     0x00, 0x00, 0x00, 0x00),
		.uval = 0xcafedeadbeefcafe,
		.start_bit = 95,
		.end_bit = 32,
		.quirks = NO_QUIRKS,
	},
	{
		.desc = "no quirks, 22 bytes",
		PBUF(0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
		     0x00, 0x00, 0xca, 0xfe, 0xde, 0xad, 0xbe, 0xef,
		     0xca, 0xfe, 0x00, 0x00, 0x00, 0x00),
		.uval = 0xcafedeadbeefcafe,
		.start_bit = 95,
		.end_bit = 32,
		.quirks = NO_QUIRKS,
	},
	{
		.desc = "no quirks, 24 bytes",
		PBUF(0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
		     0x00, 0x00, 0x00, 0x00, 0xca, 0xfe, 0xde, 0xad,
		     0xbe, 0xef, 0xca, 0xfe, 0x00, 0x00, 0x00, 0x00),
		.uval = 0xcafedeadbeefcafe,
		.start_bit = 95,
		.end_bit = 32,
		.quirks = NO_QUIRKS,
	},
	{
		.desc = "lsw32 first + little endian words, 18 bytes",
		PBUF(0x00, 0x00, 0x00, 0x00, 0xfe, 0xca, 0xef, 0xbe,
		     0xad, 0xde, 0xfe, 0xca, 0x00, 0x00, 0x00, 0x00,
		     0x00, 0x00),
		.uval = 0xcafedeadbeefcafe,
		.start_bit = 95,
		.end_bit = 32,
		.quirks = QUIRK_LSW32_IS_FIRST | QUIRK_LITTLE_ENDIAN,
	},
	{
		.desc = "lsw32 first + little endian words, 19 bytes",
		PBUF(0x00, 0x00, 0x00, 0x00, 0xfe, 0xca, 0xef, 0xbe,
		     0xad, 0xde, 0xfe, 0xca, 0x00, 0x00, 0x00, 0x00,
		     0x00, 0x00, 0x00),
		.uval = 0xcafedeadbeefcafe,
		.start_bit = 95,
		.end_bit = 32,
		.quirks = QUIRK_LSW32_IS_FIRST | QUIRK_LITTLE_ENDIAN,
	},
	{
		.desc = "lsw32 first + little endian words, 20 bytes",
		PBUF(0x00, 0x00, 0x00, 0x00, 0xfe, 0xca, 0xef, 0xbe,
		     0xad, 0xde, 0xfe, 0xca, 0x00, 0x00, 0x00, 0x00,
		     0x00, 0x00, 0x00, 0x00),
		.uval = 0xcafedeadbeefcafe,
		.start_bit = 95,
		.end_bit = 32,
		.quirks = QUIRK_LSW32_IS_FIRST | QUIRK_LITTLE_ENDIAN,
	},
	{
		.desc = "lsw32 first + little endian words, 22 bytes",
		PBUF(0x00, 0x00, 0x00, 0x00, 0xfe, 0xca, 0xef, 0xbe,
		     0xad, 0xde, 0xfe, 0xca, 0x00, 0x00, 0x00, 0x00,
		     0x00, 0x00, 0x00, 0x00, 0x00, 0x00),
		.uval = 0xcafedeadbeefcafe,
		.start_bit = 95,
		.end_bit = 32,
		.quirks = QUIRK_LSW32_IS_FIRST | QUIRK_LITTLE_ENDIAN,
	},
	{
		.desc = "lsw32 first + little endian words, 24 bytes",
		PBUF(0x00, 0x00, 0x00, 0x00, 0xfe, 0xca, 0xef, 0xbe,
		     0xad, 0xde, 0xfe, 0xca, 0x00, 0x00, 0x00, 0x00,
		     0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00),
		.uval = 0xcafedeadbeefcafe,
		.start_bit = 95,
		.end_bit = 32,
		.quirks = QUIRK_LSW32_IS_FIRST | QUIRK_LITTLE_ENDIAN,
	},
	/* These tests pack and unpack a magic 64-bit value
	 * (0x1122334455667788) at an odd starting bit (43) within an
	 * otherwise zero array of 128 bits (16 bytes). They test all possible
	 * bit layouts of the 128 bit buffer.
	 */
	{
		.desc = "no quirks, 16 bytes, non-aligned",
		PBUF(0x00, 0x00, 0x00, 0x89, 0x11, 0x9a, 0x22, 0xab,
		     0x33, 0xbc, 0x40, 0x00, 0x00, 0x00, 0x00, 0x00),
		.uval = 0x1122334455667788,
		.start_bit = 106,
		.end_bit = 43,
		.quirks = NO_QUIRKS,
	},
	{
		.desc = "lsw32 first, 16 bytes, non-aligned",
		PBUF(0x00, 0x00, 0x00, 0x00, 0x33, 0xbc, 0x40, 0x00,
		     0x11, 0x9a, 0x22, 0xab, 0x00, 0x00, 0x00, 0x89),
		.uval = 0x1122334455667788,
		.start_bit = 106,
		.end_bit = 43,
		.quirks = QUIRK_LSW32_IS_FIRST,
	},
	{
		.desc = "little endian words, 16 bytes, non-aligned",
		PBUF(0x89, 0x00, 0x00, 0x00, 0xab, 0x22, 0x9a, 0x11,
		     0x00, 0x40, 0xbc, 0x33, 0x00, 0x00, 0x00, 0x00),
		.uval = 0x1122334455667788,
		.start_bit = 106,
		.end_bit = 43,
		.quirks = QUIRK_LITTLE_ENDIAN,
	},
	{
		.desc = "lsw32 first + little endian words, 16 bytes, non-aligned",
		PBUF(0x00, 0x00, 0x00, 0x00, 0x00, 0x40, 0xbc, 0x33,
		     0xab, 0x22, 0x9a, 0x11, 0x89, 0x00, 0x00, 0x00),
		.uval = 0x1122334455667788,
		.start_bit = 106,
		.end_bit = 43,
		.quirks = QUIRK_LSW32_IS_FIRST | QUIRK_LITTLE_ENDIAN,
	},
	{
		.desc = "msb right, 16 bytes, non-aligned",
		PBUF(0x00, 0x00, 0x00, 0x91, 0x88, 0x59, 0x44, 0xd5,
		     0xcc, 0x3d, 0x02, 0x00, 0x00, 0x00, 0x00, 0x00),
		.uval = 0x1122334455667788,
		.start_bit = 106,
		.end_bit = 43,
		.quirks = QUIRK_MSB_ON_THE_RIGHT,
	},
	{
		.desc = "msb right + lsw32 first, 16 bytes, non-aligned",
		PBUF(0x00, 0x00, 0x00, 0x00, 0xcc, 0x3d, 0x02, 0x00,
		     0x88, 0x59, 0x44, 0xd5, 0x00, 0x00, 0x00, 0x91),
		.uval = 0x1122334455667788,
		.start_bit = 106,
		.end_bit = 43,
		.quirks = QUIRK_MSB_ON_THE_RIGHT | QUIRK_LSW32_IS_FIRST,
	},
	{
		.desc = "msb right + little endian words, 16 bytes, non-aligned",
		PBUF(0x91, 0x00, 0x00, 0x00, 0xd5, 0x44, 0x59, 0x88,
		     0x00, 0x02, 0x3d, 0xcc, 0x00, 0x00, 0x00, 0x00),
		.uval = 0x1122334455667788,
		.start_bit = 106,
		.end_bit = 43,
		.quirks = QUIRK_MSB_ON_THE_RIGHT | QUIRK_LITTLE_ENDIAN,
	},
	{
		.desc = "msb right + lsw32 first + little endian words, 16 bytes, non-aligned",
		PBUF(0x00, 0x00, 0x00, 0x00, 0x00, 0x02, 0x3d, 0xcc,
		     0xd5, 0x44, 0x59, 0x88, 0x91, 0x00, 0x00, 0x00),
		.uval = 0x1122334455667788,
		.start_bit = 106,
		.end_bit = 43,
		.quirks = QUIRK_MSB_ON_THE_RIGHT | QUIRK_LSW32_IS_FIRST | QUIRK_LITTLE_ENDIAN,
	},
	/* These tests pack and unpack a u64 with all bits set
	 * (0xffffffffffffffff) at an odd starting bit (43) within an
	 * otherwise zero array of 128 bits (16 bytes). They test all possible
	 * bit layouts of the 128 bit buffer.
	 */
	{
		.desc = "no quirks, 16 bytes, non-aligned, 0xff",
		PBUF(0x00, 0x00, 0x07, 0xff, 0xff, 0xff, 0xff, 0xff,
		     0xff, 0xff, 0xf8, 0x00, 0x00, 0x00, 0x00, 0x00),
		.uval = 0xffffffffffffffff,
		.start_bit = 106,
		.end_bit = 43,
		.quirks = NO_QUIRKS,
	},
	{
		.desc = "lsw32 first, 16 bytes, non-aligned, 0xff",
		PBUF(0x00, 0x00, 0x00, 0x00, 0xff, 0xff, 0xf8, 0x00,
		     0xff, 0xff, 0xff, 0xff, 0x00, 0x00, 0x07, 0xff),
		.uval = 0xffffffffffffffff,
		.start_bit = 106,
		.end_bit = 43,
		.quirks = QUIRK_LSW32_IS_FIRST,
	},
	{
		.desc = "little endian words, 16 bytes, non-aligned, 0xff",
		PBUF(0xff, 0x07, 0x00, 0x00, 0xff, 0xff, 0xff, 0xff,
		     0x00, 0xf8, 0xff, 0xff, 0x00, 0x00, 0x00, 0x00),
		.uval = 0xffffffffffffffff,
		.start_bit = 106,
		.end_bit = 43,
		.quirks = QUIRK_LITTLE_ENDIAN,
	},
	{
		.desc = "lsw32 first + little endian words, 16 bytes, non-aligned, 0xff",
		PBUF(0x00, 0x00, 0x00, 0x00, 0x00, 0xf8, 0xff, 0xff,
		     0xff, 0xff, 0xff, 0xff, 0xff, 0x07, 0x00, 0x00),
		.uval = 0xffffffffffffffff,
		.start_bit = 106,
		.end_bit = 43,
		.quirks = QUIRK_LSW32_IS_FIRST | QUIRK_LITTLE_ENDIAN,
	},
	{
		.desc = "msb right, 16 bytes, non-aligned, 0xff",
		PBUF(0x00, 0x00, 0xe0, 0xff, 0xff, 0xff, 0xff, 0xff,
		     0xff, 0xff, 0x1f, 0x00, 0x00, 0x00, 0x00, 0x00),
		.uval = 0xffffffffffffffff,
		.start_bit = 106,
		.end_bit = 43,
		.quirks = QUIRK_MSB_ON_THE_RIGHT,
	},
	{
		.desc = "msb right + lsw32 first, 16 bytes, non-aligned, 0xff",
		PBUF(0x00, 0x00, 0x00, 0x00, 0xff, 0xff, 0x1f, 0x00,
		     0xff, 0xff, 0xff, 0xff, 0x00, 0x00, 0xe0, 0xff),
		.uval = 0xffffffffffffffff,
		.start_bit = 106,
		.end_bit = 43,
		.quirks = QUIRK_MSB_ON_THE_RIGHT | QUIRK_LSW32_IS_FIRST,
	},
	{
		.desc = "msb right + little endian words, 16 bytes, non-aligned, 0xff",
		PBUF(0xff, 0xe0, 0x00, 0x00, 0xff, 0xff, 0xff, 0xff,
		     0x00, 0x1f, 0xff, 0xff, 0x00, 0x00, 0x00, 0x00),
		.uval = 0xffffffffffffffff,
		.start_bit = 106,
		.end_bit = 43,
		.quirks = QUIRK_MSB_ON_THE_RIGHT | QUIRK_LITTLE_ENDIAN,
	},
	{
		.desc = "msb right + lsw32 first + little endian words, 16 bytes, non-aligned, 0xff",
		PBUF(0x00, 0x00, 0x00, 0x00, 0x00, 0x1f, 0xff, 0xff,
		     0xff, 0xff, 0xff, 0xff, 0xff, 0xe0, 0x00, 0x00),
		.uval = 0xffffffffffffffff,
		.start_bit = 106,
		.end_bit = 43,
		.quirks = QUIRK_MSB_ON_THE_RIGHT | QUIRK_LSW32_IS_FIRST | QUIRK_LITTLE_ENDIAN,
	},
};

KUNIT_ARRAY_PARAM_DESC(packing, cases, desc);

static void packing_test_pack(struct kunit *test)
{
	const struct packing_test_case *params = test->param_value;
	u8 *pbuf;
	int err;

	pbuf = kunit_kzalloc(test, params->pbuf_size, GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, pbuf);

	err = pack(pbuf, params->uval, params->start_bit, params->end_bit,
		   params->pbuf_size, params->quirks);

	KUNIT_EXPECT_EQ_MSG(test, err, 0, "pack() returned %pe\n", ERR_PTR(err));
	KUNIT_EXPECT_MEMEQ(test, pbuf, params->pbuf, params->pbuf_size);
}

static void packing_test_unpack(struct kunit *test)
{
	const struct packing_test_case *params = test->param_value;
	u64 uval;
	int err;

	err = unpack(params->pbuf, &uval, params->start_bit, params->end_bit,
		     params->pbuf_size, params->quirks);
	KUNIT_EXPECT_EQ_MSG(test, err, 0, "unpack() returned %pe\n", ERR_PTR(err));
	KUNIT_EXPECT_EQ(test, uval, params->uval);
}

#define PACKED_BUF_SIZE 8

typedef struct __packed { u8 buf[PACKED_BUF_SIZE]; } packed_buf_t;

struct test_data {
	u32 field3;
	u16 field2;
	u16 field4;
	u16 field6;
	u8 field1;
	u8 field5;
};

static const struct packed_field_u8 test_fields[] = {
	PACKED_FIELD(63, 61, struct test_data, field1),
	PACKED_FIELD(60, 52, struct test_data, field2),
	PACKED_FIELD(51, 28, struct test_data, field3),
	PACKED_FIELD(27, 14, struct test_data, field4),
	PACKED_FIELD(13, 9, struct test_data, field5),
	PACKED_FIELD(8, 0, struct test_data, field6),
};

static void packing_test_pack_fields(struct kunit *test)
{
	const struct test_data data = {
		.field1 = 0x2,
		.field2 = 0x100,
		.field3 = 0xF00050,
		.field4 = 0x7D3,
		.field5 = 0x9,
		.field6 = 0x10B,
	};
	packed_buf_t expect = {
		.buf = { 0x50, 0x0F, 0x00, 0x05, 0x01, 0xF4, 0xD3, 0x0B },
	};
	packed_buf_t buf = {};

	pack_fields(&buf, sizeof(buf), &data, test_fields, 0);

	KUNIT_EXPECT_MEMEQ(test, &expect, &buf, sizeof(buf));
}

static void packing_test_unpack_fields(struct kunit *test)
{
	const packed_buf_t buf = {
		.buf = { 0x17, 0x28, 0x10, 0x19, 0x3D, 0xA9, 0x07, 0x9C },
	};
	struct test_data data = {};

	unpack_fields(&buf, sizeof(buf), &data, test_fields, 0);

	KUNIT_EXPECT_EQ(test, 0, data.field1);
	KUNIT_EXPECT_EQ(test, 0x172, data.field2);
	KUNIT_EXPECT_EQ(test, 0x810193, data.field3);
	KUNIT_EXPECT_EQ(test, 0x36A4, data.field4);
	KUNIT_EXPECT_EQ(test, 0x3, data.field5);
	KUNIT_EXPECT_EQ(test, 0x19C, data.field6);
}

static struct kunit_case packing_test_cases[] = {
	KUNIT_CASE_PARAM(packing_test_pack, packing_gen_params),
	KUNIT_CASE_PARAM(packing_test_unpack, packing_gen_params),
	KUNIT_CASE(packing_test_pack_fields),
	KUNIT_CASE(packing_test_unpack_fields),
	{},
};

static struct kunit_suite packing_test_suite = {
	.name = "packing",
	.test_cases = packing_test_cases,
};

kunit_test_suite(packing_test_suite);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("KUnit tests for packing library");
