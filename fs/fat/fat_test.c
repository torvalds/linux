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

static void fat_clus_to_blknr_test(struct kunit *test)
{
	struct msdos_sb_info sbi = {
		.sec_per_clus = 4,
		.data_start = 100,
	};

	KUNIT_EXPECT_EQ(test, (sector_t)100,
			fat_clus_to_blknr(&sbi, FAT_START_ENT));
	KUNIT_EXPECT_EQ(test, (sector_t)112, fat_clus_to_blknr(&sbi, 5));
}

static void fat_get_blknr_offset_test(struct kunit *test)
{
	struct msdos_sb_info sbi = {
		.dir_per_block = 16,
		.dir_per_block_bits = 4,
	};

	sector_t blknr;
	int offset;

	fat_get_blknr_offset(&sbi, 0, &blknr, &offset);
	KUNIT_EXPECT_EQ(test, (sector_t)0, blknr);
	KUNIT_EXPECT_EQ(test, 0, offset);

	fat_get_blknr_offset(&sbi, (10 << 4) | 7, &blknr, &offset);
	KUNIT_EXPECT_EQ(test, (sector_t)10, blknr);
	KUNIT_EXPECT_EQ(test, 7, offset);
}

struct fat_timestamp_testcase {
	const char *name;
	struct timespec64 ts;
	__le16 time;
	__le16 date;
	u8 cs;
	int time_offset;
};

struct fat_unix2fat_clamp_testcase {
	const char *name;
	struct timespec64 ts;
	__le16 time;
	__le16 date;
	u8 cs;
	int time_offset;
};

struct fat_truncate_atime_testcase {
	const char *name;
	struct timespec64 ts;
	struct timespec64 expected;
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

static struct fat_unix2fat_clamp_testcase unix2fat_clamp_test_cases[] = {
	{
		.name = "Clamp to earliest FAT date for 1979-12-31 23:59:59 UTC",
		.ts = {.tv_sec = 315532799LL, .tv_nsec = 0L},
		.time = cpu_to_le16(0),
		.date = cpu_to_le16(33),
		.cs = 0,
		.time_offset = 0,
	},
	{
		.name = "Clamp after time_offset=-60 pushes 1980-01-01 00:30 UTC below 1980",
		.ts = {.tv_sec = 315534600LL, .tv_nsec = 0L},
		.time = cpu_to_le16(0),
		.date = cpu_to_le16(33),
		.cs = 0,
		.time_offset = -60,
	},
	{
		.name = "Clamp to latest FAT date for 2108-01-01 00:00:00 UTC",
		.ts = {.tv_sec = 4354819200LL, .tv_nsec = 0L},
		.time = cpu_to_le16(49021),
		.date = cpu_to_le16(65439),
		.cs = 199,
		.time_offset = 0,
	},
	{
		.name = "Clamp after time_offset=60 pushes 2107-12-31 23:30 UTC beyond 2107",
		.ts = {.tv_sec = 4354817400LL, .tv_nsec = 0L},
		.time = cpu_to_le16(49021),
		.date = cpu_to_le16(65439),
		.cs = 199,
		.time_offset = 60,
	},
};

static struct fat_truncate_atime_testcase truncate_atime_test_cases[] = {
	{
		.name = "UTC atime truncates to 2004-02-29 00:00:00",
		.ts = {.tv_sec = 1078058096LL, .tv_nsec = 789000000L},
		.expected = {.tv_sec = 1078012800LL, .tv_nsec = 0L},
		.time_offset = 0,
	},
	{
		.name = "time_offset=-60 truncates 2004-02-29 00:30 UTC to previous local midnight",
		.ts = {.tv_sec = 1078014645LL, .tv_nsec = 123000000L},
		.expected = {.tv_sec = 1077930000LL, .tv_nsec = 0L},
		.time_offset = -60,
	},
	{
		.name = "time_offset=60 truncates 2004-02-29 23:30 UTC to next local midnight",
		.ts = {.tv_sec = 1078097445LL, .tv_nsec = 123000000L},
		.expected = {.tv_sec = 1078095600LL, .tv_nsec = 0L},
		.time_offset = 60,
	},
};

static void time_testcase_desc(struct fat_timestamp_testcase *t,
			       char *desc)
{
	strscpy(desc, t->name, KUNIT_PARAM_DESC_SIZE);
}

static void unix2fat_clamp_testcase_desc(struct fat_unix2fat_clamp_testcase *t,
					 char *desc)
{
	strscpy(desc, t->name, KUNIT_PARAM_DESC_SIZE);
}

static void truncate_atime_testcase_desc(struct fat_truncate_atime_testcase *t,
					 char *desc)
{
	strscpy(desc, t->name, KUNIT_PARAM_DESC_SIZE);
}

KUNIT_ARRAY_PARAM(fat_time, time_test_cases, time_testcase_desc);
KUNIT_ARRAY_PARAM(fat_unix2fat_clamp, unix2fat_clamp_test_cases,
		  unix2fat_clamp_testcase_desc);
KUNIT_ARRAY_PARAM(fat_truncate_atime, truncate_atime_test_cases,
		  truncate_atime_testcase_desc);

static void fat_test_set_time_offset(struct msdos_sb_info *sbi, int time_offset)
{
	memset(sbi, 0, sizeof(*sbi));
	sbi->options.tz_set = 1;
	sbi->options.time_offset = time_offset;
}

static void fat_time_fat2unix_test(struct kunit *test)
{
	static struct msdos_sb_info fake_sb;
	struct timespec64 ts;
	struct fat_timestamp_testcase *testcase =
		(struct fat_timestamp_testcase *)test->param_value;

	fat_test_set_time_offset(&fake_sb, testcase->time_offset);

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

	fat_test_set_time_offset(&fake_sb, testcase->time_offset);

	fat_time_unix2fat(&fake_sb, &testcase->ts,
			  &time, &date, &cs);
	KUNIT_EXPECT_EQ_MSG(test,
			    testcase->time,
			    time,
			    "Time mismatch\n");
	KUNIT_EXPECT_EQ_MSG(test,
			    testcase->date,
			    date,
			    "Date mismatch\n");
	KUNIT_EXPECT_EQ_MSG(test,
			    testcase->cs,
			    cs,
			    "Centisecond mismatch\n");
}

static void fat_time_unix2fat_clamp_test(struct kunit *test)
{
	static struct msdos_sb_info fake_sb;
	__le16 date, time;
	u8 cs;
	struct fat_unix2fat_clamp_testcase *testcase =
		(struct fat_unix2fat_clamp_testcase *)test->param_value;

	fat_test_set_time_offset(&fake_sb, testcase->time_offset);

	fat_time_unix2fat(&fake_sb, &testcase->ts, &time, &date, &cs);
	KUNIT_EXPECT_EQ_MSG(test,
			    testcase->time,
			    time,
			    "Clamped time mismatch\n");
	KUNIT_EXPECT_EQ_MSG(test,
			    testcase->date,
			    date,
			    "Clamped date mismatch\n");
	KUNIT_EXPECT_EQ_MSG(test,
			    testcase->cs,
			    cs,
			    "Clamped centisecond mismatch\n");
}

static void fat_time_unix2fat_no_csec_test(struct kunit *test)
{
	static struct msdos_sb_info fake_sb;
	struct timespec64 ts = {
		.tv_sec = 946684799LL,
		.tv_nsec = 0L,
	};
	__le16 date, time;

	fat_test_set_time_offset(&fake_sb, 0);

	fat_time_unix2fat(&fake_sb, &ts, &time, &date, NULL);
	KUNIT_EXPECT_EQ_MSG(test,
			    49021,
			    le16_to_cpu(time),
			    "Time mismatch without centiseconds\n");
	KUNIT_EXPECT_EQ_MSG(test,
			    10143,
			    le16_to_cpu(date),
			    "Date mismatch without centiseconds\n");
}

static void fat_truncate_atime_test(struct kunit *test)
{
	static struct msdos_sb_info fake_sb;
	struct timespec64 actual;
	struct fat_truncate_atime_testcase *testcase =
		(struct fat_truncate_atime_testcase *)test->param_value;

	fat_test_set_time_offset(&fake_sb, testcase->time_offset);

	actual = fat_truncate_atime(&fake_sb, &testcase->ts);
	KUNIT_EXPECT_EQ_MSG(test,
			    testcase->expected.tv_sec,
			    actual.tv_sec,
			    "Atime truncation seconds mismatch\n");
	KUNIT_EXPECT_EQ_MSG(test,
			    testcase->expected.tv_nsec,
			    actual.tv_nsec,
			    "Atime truncation nanoseconds mismatch\n");
}

static struct kunit_case fat_test_cases[] = {
	KUNIT_CASE(fat_checksum_test),
	KUNIT_CASE(fat_clus_to_blknr_test),
	KUNIT_CASE(fat_get_blknr_offset_test),
	KUNIT_CASE_PARAM(fat_time_fat2unix_test, fat_time_gen_params),
	KUNIT_CASE_PARAM(fat_time_unix2fat_test, fat_time_gen_params),
	KUNIT_CASE_PARAM(fat_time_unix2fat_clamp_test,
			 fat_unix2fat_clamp_gen_params),
	KUNIT_CASE(fat_time_unix2fat_no_csec_test),
	KUNIT_CASE_PARAM(fat_truncate_atime_test,
			 fat_truncate_atime_gen_params),
	{},
};

static struct kunit_suite fat_test_suite = {
	.name = "fat_test",
	.test_cases = fat_test_cases,
};

kunit_test_suites(&fat_test_suite);

MODULE_DESCRIPTION("KUnit tests for FAT filesystems");
MODULE_LICENSE("GPL v2");
