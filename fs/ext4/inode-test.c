// SPDX-License-Identifier: GPL-2.0
/*
 * KUnit test of ext4 inode that verify the seconds part of [a/c/m]
 * timestamps in ext4 inode structs are decoded correctly.
 */

#include <kunit/test.h>
#include <linux/kernel.h>
#include <linux/time64.h>

#include "ext4.h"

/*
 * For constructing the nonnegative timestamp lower bound value.
 * binary: 00000000 00000000 00000000 00000000
 */
#define LOWER_MSB_0 0L
/*
 * For constructing the nonnegative timestamp upper bound value.
 * binary: 01111111 11111111 11111111 11111111
 *
 */
#define UPPER_MSB_0 0x7fffffffL
/*
 * For constructing the negative timestamp lower bound value.
 * binary: 10000000 00000000 00000000 00000000
 */
#define LOWER_MSB_1 (-(UPPER_MSB_0) - 1L)  /* avoid overflow */
/*
 * For constructing the negative timestamp upper bound value.
 * binary: 11111111 11111111 11111111 11111111
 */
#define UPPER_MSB_1 (-1L)
/*
 * Upper bound for nanoseconds value supported by the encoding.
 * binary: 00111111 11111111 11111111 11111111
 */
#define MAX_NANOSECONDS ((1L << 30) - 1)

#define CASE_NAME_FORMAT "%s: msb:%x lower_bound:%x extra_bits: %x"

#define LOWER_BOUND_NEG_NO_EXTRA_BITS_CASE\
	"1901-12-13 Lower bound of 32bit < 0 timestamp, no extra bits"
#define UPPER_BOUND_NEG_NO_EXTRA_BITS_CASE\
	"1969-12-31 Upper bound of 32bit < 0 timestamp, no extra bits"
#define LOWER_BOUND_NONNEG_NO_EXTRA_BITS_CASE\
	"1970-01-01 Lower bound of 32bit >=0 timestamp, no extra bits"
#define UPPER_BOUND_NONNEG_NO_EXTRA_BITS_CASE\
	"2038-01-19 Upper bound of 32bit >=0 timestamp, no extra bits"
#define LOWER_BOUND_NEG_LO_1_CASE\
	"2038-01-19 Lower bound of 32bit <0 timestamp, lo extra sec bit on"
#define UPPER_BOUND_NEG_LO_1_CASE\
	"2106-02-07 Upper bound of 32bit <0 timestamp, lo extra sec bit on"
#define LOWER_BOUND_NONNEG_LO_1_CASE\
	"2106-02-07 Lower bound of 32bit >=0 timestamp, lo extra sec bit on"
#define UPPER_BOUND_NONNEG_LO_1_CASE\
	"2174-02-25 Upper bound of 32bit >=0 timestamp, lo extra sec bit on"
#define LOWER_BOUND_NEG_HI_1_CASE\
	"2174-02-25 Lower bound of 32bit <0 timestamp, hi extra sec bit on"
#define UPPER_BOUND_NEG_HI_1_CASE\
	"2242-03-16 Upper bound of 32bit <0 timestamp, hi extra sec bit on"
#define LOWER_BOUND_NONNEG_HI_1_CASE\
	"2242-03-16 Lower bound of 32bit >=0 timestamp, hi extra sec bit on"
#define UPPER_BOUND_NONNEG_HI_1_CASE\
	"2310-04-04 Upper bound of 32bit >=0 timestamp, hi extra sec bit on"
#define UPPER_BOUND_NONNEG_HI_1_NS_1_CASE\
	"2310-04-04 Upper bound of 32bit>=0 timestamp, hi extra sec bit 1. 1 ns"
#define LOWER_BOUND_NONNEG_HI_1_NS_MAX_CASE\
	"2378-04-22 Lower bound of 32bit>= timestamp. Extra sec bits 1. Max ns"
#define LOWER_BOUND_NONNEG_EXTRA_BITS_1_CASE\
	"2378-04-22 Lower bound of 32bit >=0 timestamp. All extra sec bits on"
#define UPPER_BOUND_NONNEG_EXTRA_BITS_1_CASE\
	"2446-05-10 Upper bound of 32bit >=0 timestamp. All extra sec bits on"

struct timestamp_expectation {
	const char *test_case_name;
	struct timespec64 expected;
	u32 extra_bits;
	bool msb_set;
	bool lower_bound;
};

static const struct timestamp_expectation test_data[] = {
	{
		.test_case_name = LOWER_BOUND_NEG_NO_EXTRA_BITS_CASE,
		.msb_set = true,
		.lower_bound = true,
		.extra_bits = 0,
		.expected = {.tv_sec = -0x80000000LL, .tv_nsec = 0L},
	},

	{
		.test_case_name = UPPER_BOUND_NEG_NO_EXTRA_BITS_CASE,
		.msb_set = true,
		.lower_bound = false,
		.extra_bits = 0,
		.expected = {.tv_sec = -1LL, .tv_nsec = 0L},
	},

	{
		.test_case_name = LOWER_BOUND_NONNEG_NO_EXTRA_BITS_CASE,
		.msb_set = false,
		.lower_bound = true,
		.extra_bits = 0,
		.expected = {0LL, 0L},
	},

	{
		.test_case_name = UPPER_BOUND_NONNEG_NO_EXTRA_BITS_CASE,
		.msb_set = false,
		.lower_bound = false,
		.extra_bits = 0,
		.expected = {.tv_sec = 0x7fffffffLL, .tv_nsec = 0L},
	},

	{
		.test_case_name = LOWER_BOUND_NEG_LO_1_CASE,
		.msb_set = true,
		.lower_bound = true,
		.extra_bits = 1,
		.expected = {.tv_sec = 0x80000000LL, .tv_nsec = 0L},
	},

	{
		.test_case_name = UPPER_BOUND_NEG_LO_1_CASE,
		.msb_set = true,
		.lower_bound = false,
		.extra_bits = 1,
		.expected = {.tv_sec = 0xffffffffLL, .tv_nsec = 0L},
	},

	{
		.test_case_name = LOWER_BOUND_NONNEG_LO_1_CASE,
		.msb_set = false,
		.lower_bound = true,
		.extra_bits = 1,
		.expected = {.tv_sec = 0x100000000LL, .tv_nsec = 0L},
	},

	{
		.test_case_name = UPPER_BOUND_NONNEG_LO_1_CASE,
		.msb_set = false,
		.lower_bound = false,
		.extra_bits = 1,
		.expected = {.tv_sec = 0x17fffffffLL, .tv_nsec = 0L},
	},

	{
		.test_case_name = LOWER_BOUND_NEG_HI_1_CASE,
		.msb_set = true,
		.lower_bound = true,
		.extra_bits =  2,
		.expected = {.tv_sec = 0x180000000LL, .tv_nsec = 0L},
	},

	{
		.test_case_name = UPPER_BOUND_NEG_HI_1_CASE,
		.msb_set = true,
		.lower_bound = false,
		.extra_bits = 2,
		.expected = {.tv_sec = 0x1ffffffffLL, .tv_nsec = 0L},
	},

	{
		.test_case_name = LOWER_BOUND_NONNEG_HI_1_CASE,
		.msb_set = false,
		.lower_bound = true,
		.extra_bits = 2,
		.expected = {.tv_sec = 0x200000000LL, .tv_nsec = 0L},
	},

	{
		.test_case_name = UPPER_BOUND_NONNEG_HI_1_CASE,
		.msb_set = false,
		.lower_bound = false,
		.extra_bits = 2,
		.expected = {.tv_sec = 0x27fffffffLL, .tv_nsec = 0L},
	},

	{
		.test_case_name = UPPER_BOUND_NONNEG_HI_1_NS_1_CASE,
		.msb_set = false,
		.lower_bound = false,
		.extra_bits = 6,
		.expected = {.tv_sec = 0x27fffffffLL, .tv_nsec = 1L},
	},

	{
		.test_case_name = LOWER_BOUND_NONNEG_HI_1_NS_MAX_CASE,
		.msb_set = false,
		.lower_bound = true,
		.extra_bits = 0xFFFFFFFF,
		.expected = {.tv_sec = 0x300000000LL,
			     .tv_nsec = MAX_NANOSECONDS},
	},

	{
		.test_case_name = LOWER_BOUND_NONNEG_EXTRA_BITS_1_CASE,
		.msb_set = false,
		.lower_bound = true,
		.extra_bits = 3,
		.expected = {.tv_sec = 0x300000000LL, .tv_nsec = 0L},
	},

	{
		.test_case_name = UPPER_BOUND_NONNEG_EXTRA_BITS_1_CASE,
		.msb_set = false,
		.lower_bound = false,
		.extra_bits = 3,
		.expected = {.tv_sec = 0x37fffffffLL, .tv_nsec = 0L},
	}
};

static void timestamp_expectation_to_desc(const struct timestamp_expectation *t,
					  char *desc)
{
	strscpy(desc, t->test_case_name, KUNIT_PARAM_DESC_SIZE);
}

KUNIT_ARRAY_PARAM(ext4_inode, test_data, timestamp_expectation_to_desc);

static time64_t get_32bit_time(const struct timestamp_expectation * const test)
{
	if (test->msb_set) {
		if (test->lower_bound)
			return LOWER_MSB_1;

		return UPPER_MSB_1;
	}

	if (test->lower_bound)
		return LOWER_MSB_0;
	return UPPER_MSB_0;
}


/*
 *  Test data is derived from the table in the Inode Timestamps section of
 *  Documentation/filesystems/ext4/inodes.rst.
 */
static void inode_test_xtimestamp_decoding(struct kunit *test)
{
	struct timespec64 timestamp;

	struct timestamp_expectation *test_param =
			(struct timestamp_expectation *)(test->param_value);

	timestamp.tv_sec = get_32bit_time(test_param);
	ext4_decode_extra_time(&timestamp,
			       cpu_to_le32(test_param->extra_bits));

	KUNIT_EXPECT_EQ_MSG(test,
			    test_param->expected.tv_sec,
			    timestamp.tv_sec,
			    CASE_NAME_FORMAT,
			    test_param->test_case_name,
			    test_param->msb_set,
			    test_param->lower_bound,
			    test_param->extra_bits);
	KUNIT_EXPECT_EQ_MSG(test,
			    test_param->expected.tv_nsec,
			    timestamp.tv_nsec,
			    CASE_NAME_FORMAT,
			    test_param->test_case_name,
			    test_param->msb_set,
			    test_param->lower_bound,
			    test_param->extra_bits);
}

static struct kunit_case ext4_inode_test_cases[] = {
	KUNIT_CASE_PARAM(inode_test_xtimestamp_decoding, ext4_inode_gen_params),
	{}
};

static struct kunit_suite ext4_inode_test_suite = {
	.name = "ext4_inode_test",
	.test_cases = ext4_inode_test_cases,
};

kunit_test_suites(&ext4_inode_test_suite);

MODULE_LICENSE("GPL v2");
