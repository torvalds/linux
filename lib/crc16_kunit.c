// SPDX-License-Identifier: GPL-2.0
/*
 * KUnits tests for CRC16.
 *
 * Copyright (C) 2024, LKCAMP
 * Author: Vinicius Peixoto <vpeixoto@lkcamp.dev>
 * Author: Fabricio Gasperin <fgasperin@lkcamp.dev>
 * Author: Enzo Bertoloti <ebertoloti@lkcamp.dev>
 */
#include <kunit/test.h>
#include <linux/crc16.h>
#include <linux/prandom.h>

#define CRC16_KUNIT_DATA_SIZE 4096
#define CRC16_KUNIT_TEST_SIZE 100
#define CRC16_KUNIT_SEED 0x12345678

/**
 * struct crc16_test - CRC16 test data
 * @crc: initial input value to CRC16
 * @start: Start index within the data buffer
 * @length: Length of the data
 */
static struct crc16_test {
	u16 crc;
	u16 start;
	u16 length;
} tests[CRC16_KUNIT_TEST_SIZE];

u8 data[CRC16_KUNIT_DATA_SIZE];


/* Naive implementation of CRC16 for validation purposes */
static inline u16 _crc16_naive_byte(u16 crc, u8 data)
{
	u8 i = 0;

	crc ^= (u16) data;
	for (i = 0; i < 8; i++) {
		if (crc & 0x01)
			crc = (crc >> 1) ^ 0xa001;
		else
			crc = crc >> 1;
	}

	return crc;
}


static inline u16 _crc16_naive(u16 crc, u8 *buffer, size_t len)
{
	while (len--)
		crc = _crc16_naive_byte(crc, *buffer++);
	return crc;
}


/* Small helper for generating pseudorandom 16-bit data */
static inline u16 _rand16(void)
{
	static u32 rand = CRC16_KUNIT_SEED;

	rand = next_pseudo_random32(rand);
	return rand & 0xFFFF;
}


static int crc16_init_test_data(struct kunit_suite *suite)
{
	size_t i;

	/* Fill the data buffer with random bytes */
	for (i = 0; i < CRC16_KUNIT_DATA_SIZE; i++)
		data[i] = _rand16() & 0xFF;

	/* Generate random test data while ensuring the random
	 * start + length values won't overflow the 4096-byte
	 * buffer (0x7FF * 2 = 0xFFE < 0x1000)
	 */
	for (size_t i = 0; i < CRC16_KUNIT_TEST_SIZE; i++) {
		tests[i].crc = _rand16();
		tests[i].start = _rand16() & 0x7FF;
		tests[i].length = _rand16() & 0x7FF;
	}

	return 0;
}

static void crc16_test_empty(struct kunit *test)
{
	u16 crc;

	/* The result for empty data should be the same as the
	 * initial crc
	 */
	crc = crc16(0x00, data, 0);
	KUNIT_EXPECT_EQ(test, crc, 0);
	crc = crc16(0xFF, data, 0);
	KUNIT_EXPECT_EQ(test, crc, 0xFF);
}

static void crc16_test_correctness(struct kunit *test)
{
	size_t i;
	u16 crc, crc_naive;

	for (i = 0; i < CRC16_KUNIT_TEST_SIZE; i++) {
		/* Compare results with the naive crc16 implementation */
		crc = crc16(tests[i].crc, data + tests[i].start,
			    tests[i].length);
		crc_naive = _crc16_naive(tests[i].crc, data + tests[i].start,
					 tests[i].length);
		KUNIT_EXPECT_EQ(test, crc, crc_naive);
	}
}


static void crc16_test_combine(struct kunit *test)
{
	size_t i, j;
	u16 crc, crc_naive;

	/* Make sure that combining two consecutive crc16 calculations
	 * yields the same result as calculating the crc16 for the whole thing
	 */
	for (i = 0; i < CRC16_KUNIT_TEST_SIZE; i++) {
		crc_naive = crc16(tests[i].crc, data + tests[i].start, tests[i].length);
		for (j = 0; j < tests[i].length; j++) {
			crc = crc16(tests[i].crc, data + tests[i].start, j);
			crc = crc16(crc, data + tests[i].start + j, tests[i].length - j);
			KUNIT_EXPECT_EQ(test, crc, crc_naive);
		}
	}
}


static struct kunit_case crc16_test_cases[] = {
	KUNIT_CASE(crc16_test_empty),
	KUNIT_CASE(crc16_test_combine),
	KUNIT_CASE(crc16_test_correctness),
	{},
};

static struct kunit_suite crc16_test_suite = {
	.name = "crc16",
	.test_cases = crc16_test_cases,
	.suite_init = crc16_init_test_data,
};
kunit_test_suite(crc16_test_suite);

MODULE_AUTHOR("Fabricio Gasperin <fgasperin@lkcamp.dev>");
MODULE_AUTHOR("Vinicius Peixoto <vpeixoto@lkcamp.dev>");
MODULE_AUTHOR("Enzo Bertoloti <ebertoloti@lkcamp.dev>");
MODULE_DESCRIPTION("Unit tests for crc16");
MODULE_LICENSE("GPL");
