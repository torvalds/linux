// SPDX-License-Identifier: GPL-2.0
/*
 * KUnit tests for the Dynamic Interrupt Moderation library
 */

#include <kunit/test.h>
#include <linux/dim.h>
#include <linux/ktime.h>
#include <linux/module.h>

struct dim_calc_stats_case {
	const char *name;
	u32 delta_us;
	u32 start_pkts, end_pkts;
	u32 start_bytes, end_bytes;
	u32 start_comps, end_comps;
	int ppms, bpms, cpms;
};

static const struct dim_calc_stats_case dim_calc_stats_cases[] = {
	{
		.name = "small",
		.delta_us = 1000,
		.end_pkts = 640, .end_bytes = 640 * 1500, .end_comps = 64,
		.ppms = 640, .bpms = 960000, .cpms = 64,
	},
	{
		.name = "round_up",
		.delta_us = 3000,
		.end_pkts = 10, .end_bytes = 10, .end_comps = 1,
		.ppms = 4, .bpms = 4, .cpms = 1,
	},
	{
		.name = "counter_wrap",
		.delta_us = 1000,
		.start_pkts = 0xffffff00, .end_pkts = 0x100,
		.start_bytes = 0xfffff000, .end_bytes = 0x1000,
		.start_comps = 0xfffffff0, .end_comps = 0x10,
		.ppms = 0x200, .bpms = 0x2000, .cpms = 0x20,
	},
	{
		/* 30 MB in 10 ms (24 Gbit/s): nbytes * 1000 exceeds 32 bits */
		.name = "many_bytes",
		.delta_us = 10000,
		.end_pkts = 20000, .end_bytes = 30000000, .end_comps = 64,
		.ppms = 2000, .bpms = 3000000, .cpms = 7,
	},
	{
		/* 4.3 MB in 16 ms (2.15 Gbit/s), just above the 32-bit limit */
		.name = "bytes_32bit_limit",
		.delta_us = 16000,
		.end_pkts = 2900, .end_bytes = 4300000, .end_comps = 64,
		.ppms = 182, .bpms = 268750, .cpms = 4,
	},
	{
		/* 5 MB in 40 ms: a 1 Gbit/s link at line rate */
		.name = "gigabit",
		.delta_us = 40000,
		.end_pkts = 3300, .end_bytes = 5000000, .end_comps = 64,
		.ppms = 83, .bpms = 125000, .cpms = 2,
	},
	{
		/* 5 million packets and completions in 2 s */
		.name = "many_packets",
		.delta_us = 2000000,
		.end_pkts = 5000000, .end_bytes = 5000000, .end_comps = 5000000,
		.ppms = 2500, .bpms = 2500, .cpms = 2500,
	},
};

static void dim_calc_stats_case_desc(const struct dim_calc_stats_case *t,
				     char *desc)
{
	strscpy(desc, t->name, KUNIT_PARAM_DESC_SIZE);
}

KUNIT_ARRAY_PARAM(dim_calc_stats, dim_calc_stats_cases,
		  dim_calc_stats_case_desc);

static void dim_calc_stats_test(struct kunit *test)
{
	const struct dim_calc_stats_case *t = test->param_value;
	struct dim_sample start = {}, end = {};
	struct dim_stats stats = {};

	dim_update_sample_with_comps(0, t->start_pkts, t->start_bytes,
				     t->start_comps, &start);
	dim_update_sample_with_comps(DIM_NEVENTS, t->end_pkts, t->end_bytes,
				     t->end_comps, &end);
	/* dim_update_sample() stamps ktime_get(); use fixed times instead */
	start.time = ktime_set(1000, 0);
	end.time = ktime_add_us(start.time, t->delta_us);

	KUNIT_ASSERT_TRUE(test, dim_calc_stats(&start, &end, &stats));
	KUNIT_EXPECT_EQ(test, stats.ppms, t->ppms);
	KUNIT_EXPECT_EQ(test, stats.bpms, t->bpms);
	KUNIT_EXPECT_EQ(test, stats.cpms, t->cpms);
	KUNIT_EXPECT_EQ(test, stats.epms,
			(int)DIV_ROUND_UP(DIM_NEVENTS * USEC_PER_MSEC,
					  t->delta_us));
}

static void dim_calc_stats_no_time_test(struct kunit *test)
{
	struct dim_sample sample = {};
	struct dim_stats stats = {};

	dim_update_sample_with_comps(0, 100, 1000, 10, &sample);
	KUNIT_EXPECT_FALSE(test, dim_calc_stats(&sample, &sample, &stats));
}

static struct kunit_case dim_test_cases[] = {
	KUNIT_CASE_PARAM(dim_calc_stats_test, dim_calc_stats_gen_params),
	KUNIT_CASE(dim_calc_stats_no_time_test),
	{}
};

static struct kunit_suite dim_test_suite = {
	.name = "dim",
	.test_cases = dim_test_cases,
};

kunit_test_suite(dim_test_suite);

MODULE_DESCRIPTION("KUnit tests for the DIM library");
MODULE_LICENSE("GPL");
