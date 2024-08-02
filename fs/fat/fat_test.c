// SPDX-License-Identifier: GPL-2.0
/*
 * KUnit tests for FAT filesystems.
 *
 * Copyright (C) 2020 Google LLC.
 * Author: David Gow <davidgow@google.com>
 */

#include <kunit/test.h>

#include "fat.h"

static void fat_checksum_test(struct kunit *test)
{
	/* With no extension. */
	KUNIT_EXPECT_EQ(test, fat_checksum("VMLINUX    "), (u8)44);
	/* With 3-letter extension. */
	KUNIT_EXPECT_EQ(test, fat_checksum("README  TXT"), (u8)115);
	/* With short (1-letter) extension. */
	KUNIT_EXPECT_EQ(test, fat_checksum("ABCDEFGHA  "), (u8)98);
}

struct fat_timestamp_testcase {
	const char *name;
	struct timespec64 ts;
	__le16 time;
	__le16 date;
	u8 cs;
	int time_offset;
};

static struct fat_timestamp_testcase time_test_cases[] = {
	{
		.name = "Earliest possible UTC (1980-01-01 00:00:00)",
		.ts = {.tv_sec = 315532800LL, .tv_nsec = 0L},
		.time = cpu_to_le16(0),
		.date = cpu_to_le16(33),
		.cs = 0,
		.time_offset = 0,
	},
	{
		.name = "Latest possible UTC (2107-12-31 23:59:58)",
		.ts = {.tv_sec = 4354819198LL, .tv_nsec = 0L},
		.time = cpu_to_le16(49021),
		.date = cpu_to_le16(65439),
		.cs = 0,
		.time_offset = 0,
	},
	{
		.name = "Earliest possible (UTC-11) (== 1979-12-31 13:00:00 UTC)",
		.ts = {.tv_sec = 315493200LL, .tv_nsec = 0L},
		.time = cpu_to_le16(0),
		.date = cpu_to_le16(33),
		.cs = 0,
		.time_offset = 11 * 60,
	},
	{
		.name = "Latest possible (UTC+11) (== 2108-01-01 10:59:58 UTC)",
		.ts = {.tv_sec = 4354858798LL, .tv_nsec = 0L},
		.time = cpu_to_le16(49021),
		.date = cpu_to_le16(65439),
		.cs = 0,
		.time_offset = -11 * 60,
	},
	{
		.name = "Leap Day / Year (1996-02-29 00:00:00)",
		.ts = {.tv_sec = 825552000LL, .tv_nsec = 0L},
		.time = cpu_to_le16(0),
		.date = cpu_to_le16(8285),
		.cs = 0,
		.time_offset = 0,
	},
	{
		.name = "Year 2000 is leap year (2000-02-29 00:00:00)",
		.ts = {.tv_sec = 951782400LL, .tv_nsec = 0L},
		.time = cpu_to_le16(0),
		.date = cpu_to_le16(10333),
		.cs = 0,
		.time_offset = 0,
	},
	{
		.name = "Year 2100 not leap year (2100-03-01 00:00:00)",
		.ts = {.tv_sec = 4107542400LL, .tv_nsec = 0L},
		.time = cpu_to_le16(0),
		.date = cpu_to_le16(61537),
		.cs = 0,
		.time_offset = 0,
	},
	{
		.name = "Leap year + timezone UTC+1 (== 2004-02-29 00:30:00 UTC)",
		.ts = {.tv_sec = 1078014600LL, .tv_nsec = 0L},
		.time = cpu_to_le16(48064),
		.date = cpu_to_le16(12380),
		.cs = 0,
		.time_offset = -60,
	},
	{
		.name = "Leap year + timezone UTC-1 (== 2004-02-29 23:30:00 UTC)",
		.ts = {.tv_sec = 1078097400LL, .tv_nsec = 0L},
		.time = cpu_to_le16(960),
		.date = cpu_to_le16(12385),
		.cs = 0,
		.time_offset = 60,
	},
	{
		.name = "VFAT odd-second resolution (1999-12-31 23:59:59)",
		.ts = {.tv_sec = 946684799LL, .tv_nsec = 0L},
		.time = cpu_to_le16(49021),
		.date = cpu_to_le16(10143),
		.cs = 100,
		.time_offset = 0,
	},
	{
		.name = "VFAT 10ms resolution (1980-01-01 00:00:00:0010)",
		.ts = {.tv_sec = 315532800LL, .tv_nsec = 10000000L},
		.time = cpu_to_le16(0),
		.date = cpu_to_le16(33),
		.cs = 1,
		.time_offset = 0,
	},
};

static void time_testcase_desc(struct fat_timestamp_testcase *t,
			       char *desc)
{
	strscpy(desc, t->name, KUNIT_PARAM_DESC_SIZE);
}

KUNIT_ARRAY_PARAM(fat_time, time_test_cases, time_testcase_desc);

static void fat_time_fat2unix_test(struct kunit *test)
{
	static struct msdos_sb_info fake_sb;
	struct timespec64 ts;
	struct fat_timestamp_testcase *testcase =
		(struct fat_timestamp_testcase *)test->param_value;

	fake_sb.options.tz_set = 1;
	fake_sb.options.time_offset = testcase->time_offset;

	fat_time_fat2unix(&fake_sb, &ts,
			  testcase->time,
			  testcase->date,
			  testcase->cs);
	KUNIT_EXPECT_EQ_MSG(test,
			    testcase->ts.tv_sec,
			    ts.tv_sec,
			    "Timestamp mismatch (seconds)\n");
	KUNIT_EXPECT_EQ_MSG(test,
			    testcase->ts.tv_nsec,
			    ts.tv_nsec,
			    "Timestamp mismatch (nanoseconds)\n");
}

static void fat_time_unix2fat_test(struct kunit *test)
{
	static struct msdos_sb_info fake_sb;
	__le16 date, time;
	u8 cs;
	struct fat_timestamp_testcase *testcase =
		(struct fat_timestamp_testcase *)test->param_value;

	fake_sb.options.tz_set = 1;
	fake_sb.options.time_offset = testcase->time_offset;

	fat_time_unix2fat(&fake_sb, &testcase->ts,
			  &time, &date, &cs);
	KUNIT_EXPECT_EQ_MSG(test,
			    le16_to_cpu(testcase->time),
			    le16_to_cpu(time),
			    "Time mismatch\n");
	KUNIT_EXPECT_EQ_MSG(test,
			    le16_to_cpu(testcase->date),
			    le16_to_cpu(date),
			    "Date mismatch\n");
	KUNIT_EXPECT_EQ_MSG(test,
			    testcase->cs,
			    cs,
			    "Centisecond mismatch\n");
}

static struct kunit_case fat_test_cases[] = {
	KUNIT_CASE(fat_checksum_test),
	KUNIT_CASE_PARAM(fat_time_fat2unix_test, fat_time_gen_params),
	KUNIT_CASE_PARAM(fat_time_unix2fat_test, fat_time_gen_params),
	{},
};

static struct kunit_suite fat_test_suite = {
	.name = "fat_test",
	.test_cases = fat_test_cases,
};

kunit_test_suites(&fat_test_suite);

MODULE_DESCRIPTION("KUnit tests for FAT filesystems");
MODULE_LICENSE("GPL v2");
