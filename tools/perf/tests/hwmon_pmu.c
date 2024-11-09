// SPDX-License-Identifier: (LGPL-2.1 OR BSD-2-Clause)
#include "debug.h"
#include "hwmon_pmu.h"
#include "tests.h"
#include <linux/compiler.h>
#include <linux/kernel.h>
#include <linux/string.h>

static int test__parse_hwmon_filename(struct test_suite *test __maybe_unused,
				      int subtest __maybe_unused)
{
	const struct hwmon_parse_test {
		const char *filename;
		enum hwmon_type type;
		int number;
		enum hwmon_item item;
		bool alarm;
		bool parse_ok;
	} tests[] = {
		{
			.filename = "cpu0_accuracy",
			.type = HWMON_TYPE_CPU,
			.number = 0,
			.item = HWMON_ITEM_ACCURACY,
			.alarm = false,
			.parse_ok = true,
		},
		{
			.filename = "temp1_input",
			.type = HWMON_TYPE_TEMP,
			.number = 1,
			.item = HWMON_ITEM_INPUT,
			.alarm = false,
			.parse_ok = true,
		},
		{
			.filename = "fan2_vid",
			.type = HWMON_TYPE_FAN,
			.number = 2,
			.item = HWMON_ITEM_VID,
			.alarm = false,
			.parse_ok = true,
		},
		{
			.filename = "power3_crit_alarm",
			.type = HWMON_TYPE_POWER,
			.number = 3,
			.item = HWMON_ITEM_CRIT,
			.alarm = true,
			.parse_ok = true,
		},
		{
			.filename = "intrusion4_average_interval_min_alarm",
			.type = HWMON_TYPE_INTRUSION,
			.number = 4,
			.item = HWMON_ITEM_AVERAGE_INTERVAL_MIN,
			.alarm = true,
			.parse_ok = true,
		},
		{
			.filename = "badtype5_baditem",
			.type = HWMON_TYPE_NONE,
			.number = 5,
			.item = HWMON_ITEM_NONE,
			.alarm = false,
			.parse_ok = false,
		},
		{
			.filename = "humidity6_baditem",
			.type = HWMON_TYPE_NONE,
			.number = 6,
			.item = HWMON_ITEM_NONE,
			.alarm = false,
			.parse_ok = false,
		},
	};

	for (size_t i = 0; i < ARRAY_SIZE(tests); i++) {
		enum hwmon_type type;
		int number;
		enum hwmon_item item;
		bool alarm;

		TEST_ASSERT_EQUAL("parse_hwmon_filename",
				parse_hwmon_filename(
					tests[i].filename,
					&type,
					&number,
					&item,
					&alarm),
				tests[i].parse_ok
			);
		if (tests[i].parse_ok) {
			TEST_ASSERT_EQUAL("parse_hwmon_filename type", type, tests[i].type);
			TEST_ASSERT_EQUAL("parse_hwmon_filename number", number, tests[i].number);
			TEST_ASSERT_EQUAL("parse_hwmon_filename item", item, tests[i].item);
			TEST_ASSERT_EQUAL("parse_hwmon_filename alarm", alarm, tests[i].alarm);
		}
	}
	return TEST_OK;
}

static struct test_case tests__hwmon_pmu[] = {
	TEST_CASE("Basic parsing test", parse_hwmon_filename),
	{	.name = NULL, }
};

struct test_suite suite__hwmon_pmu = {
	.desc = "Hwmon PMU",
	.test_cases = tests__hwmon_pmu,
};
