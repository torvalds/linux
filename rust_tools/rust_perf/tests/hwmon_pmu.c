// SPDX-License-Identifier: (LGPL-2.1 OR BSD-2-Clause)
#include "debug.h"
#include "evlist.h"
#include "hwmon_pmu.h"
#include "parse-events.h"
#include "tests.h"
#include <fcntl.h>
#include <sys/stat.h>
#include <linux/compiler.h>
#include <linux/kernel.h>
#include <linux/string.h>

static const struct test_event {
	const char *name;
	const char *alias;
	union hwmon_pmu_event_key key;
} test_events[] = {
	{
		"temp_test_hwmon_event1",
		"temp1",
		.key = {
			.num = 1,
			.type = 10
		},
	},
	{
		"temp_test_hwmon_event2",
		"temp2",
		.key = {
			.num = 2,
			.type = 10
		},
	},
};

/* Cleanup test PMU directory. */
static int test_pmu_put(const char *dir, struct perf_pmu *hwm)
{
	char buf[PATH_MAX + 20];
	int ret;

	if (scnprintf(buf, sizeof(buf), "rm -fr %s", dir) < 0) {
		pr_err("Failure to set up buffer for \"%s\"\n", dir);
		return -EINVAL;
	}
	ret = system(buf);
	if (ret)
		pr_err("Failure to \"%s\"\n", buf);

	list_del(&hwm->list);
	perf_pmu__delete(hwm);
	return ret;
}

/*
 * Prepare test PMU directory data, normally exported by kernel at
 * /sys/class/hwmon/hwmon<number>/. Give as input a buffer to hold the file
 * path, the result is PMU loaded using that directory.
 */
static struct perf_pmu *test_pmu_get(char *dir, size_t sz)
{
	const char *test_hwmon_name_nl = "A test hwmon PMU\n";
	const char *test_hwmon_name = "A test hwmon PMU";
	/* Simulated hwmon items. */
	const struct test_item {
		const char *name;
		const char *value;
	} test_items[] = {
		{ "temp1_label", "test hwmon event1\n", },
		{ "temp1_input", "40000\n", },
		{ "temp2_label", "test hwmon event2\n", },
		{ "temp2_input", "50000\n", },
	};
	int hwmon_dirfd = -1, test_dirfd = -1, file;
	struct perf_pmu *hwm = NULL;
	ssize_t len;

	/* Create equivalent of sysfs mount point. */
	scnprintf(dir, sz, "/tmp/perf-hwmon-pmu-test-XXXXXX");
	if (!mkdtemp(dir)) {
		pr_err("mkdtemp failed\n");
		dir[0] = '\0';
		return NULL;
	}
	test_dirfd = open(dir, O_PATH|O_DIRECTORY);
	if (test_dirfd < 0) {
		pr_err("Failed to open test directory \"%s\"\n", dir);
		goto err_out;
	}

	/* Create the test hwmon directory and give it a name. */
	if (mkdirat(test_dirfd, "hwmon1234", 0755) < 0) {
		pr_err("Failed to mkdir hwmon directory\n");
		goto err_out;
	}
	strncat(dir, "/hwmon1234", sz - strlen(dir));
	hwmon_dirfd = open(dir, O_PATH|O_DIRECTORY);
	if (hwmon_dirfd < 0) {
		pr_err("Failed to open test hwmon directory \"%s\"\n", dir);
		goto err_out;
	}
	file = openat(hwmon_dirfd, "name", O_WRONLY | O_CREAT, 0600);
	if (file < 0) {
		pr_err("Failed to open for writing file \"name\"\n");
		goto err_out;
	}
	len = strlen(test_hwmon_name_nl);
	if (write(file, test_hwmon_name_nl, len) < len) {
		close(file);
		pr_err("Failed to write to 'name' file\n");
		goto err_out;
	}
	close(file);

	/* Create test hwmon files. */
	for (size_t i = 0; i < ARRAY_SIZE(test_items); i++) {
		const struct test_item *item = &test_items[i];

		file = openat(hwmon_dirfd, item->name, O_WRONLY | O_CREAT, 0600);
		if (file < 0) {
			pr_err("Failed to open for writing file \"%s\"\n", item->name);
			goto err_out;
		}

		if (write(file, item->value, strlen(item->value)) < 0) {
			pr_err("Failed to write to file \"%s\"\n", item->name);
			close(file);
			goto err_out;
		}
		close(file);
	}

	/* Make the PMU reading the files created above. */
	hwm = perf_pmus__add_test_hwmon_pmu(dir, "hwmon1234", test_hwmon_name);
	if (!hwm)
		pr_err("Test hwmon creation failed\n");

err_out:
	if (!hwm) {
		test_pmu_put(dir, hwm);
	}
	if (test_dirfd >= 0)
		close(test_dirfd);
	if (hwmon_dirfd >= 0)
		close(hwmon_dirfd);
	return hwm;
}

static int do_test(size_t i, bool with_pmu, bool with_alias)
{
	const char *test_event = with_alias ? test_events[i].alias : test_events[i].name;
	struct evlist *evlist = evlist__new();
	struct evsel *evsel;
	struct parse_events_error err;
	int ret;
	char str[128];
	bool found = false;

	if (!evlist) {
		pr_err("evlist allocation failed\n");
		return TEST_FAIL;
	}

	if (with_pmu)
		snprintf(str, sizeof(str), "hwmon_a_test_hwmon_pmu/%s/", test_event);
	else
		strlcpy(str, test_event, sizeof(str));

	pr_debug("Testing '%s'\n", str);
	parse_events_error__init(&err);
	ret = parse_events(evlist, str, &err);
	if (ret) {
		pr_debug("FAILED %s:%d failed to parse event '%s', err %d\n",
			 __FILE__, __LINE__, str, ret);
		parse_events_error__print(&err, str);
		ret = TEST_FAIL;
		goto out;
	}

	ret = TEST_OK;
	if (with_pmu ? (evlist->core.nr_entries != 1) : (evlist->core.nr_entries < 1)) {
		pr_debug("FAILED %s:%d Unexpected number of events for '%s' of %d\n",
			 __FILE__, __LINE__, str, evlist->core.nr_entries);
		ret = TEST_FAIL;
		goto out;
	}

	evlist__for_each_entry(evlist, evsel) {
		if (!evsel->pmu || !evsel->pmu->name ||
		    strcmp(evsel->pmu->name, "hwmon_a_test_hwmon_pmu"))
			continue;

		if (evsel->core.attr.config != (u64)test_events[i].key.type_and_num) {
			pr_debug("FAILED %s:%d Unexpected config for '%s', %lld != %ld\n",
				__FILE__, __LINE__, str,
				evsel->core.attr.config,
				test_events[i].key.type_and_num);
			ret = TEST_FAIL;
			goto out;
		}
		found = true;
	}

	if (!found) {
		pr_debug("FAILED %s:%d Didn't find hwmon event '%s' in parsed evsels\n",
			 __FILE__, __LINE__, str);
		ret = TEST_FAIL;
	}

out:
	parse_events_error__exit(&err);
	evlist__delete(evlist);
	return ret;
}

static int test__hwmon_pmu(bool with_pmu)
{
	char dir[PATH_MAX];
	struct perf_pmu *pmu = test_pmu_get(dir, sizeof(dir));
	int ret = TEST_OK;

	if (!pmu)
		return TEST_FAIL;

	for (size_t i = 0; i < ARRAY_SIZE(test_events); i++) {
		ret = do_test(i, with_pmu, /*with_alias=*/false);

		if (ret != TEST_OK)
			break;

		ret = do_test(i, with_pmu, /*with_alias=*/true);

		if (ret != TEST_OK)
			break;
	}
	test_pmu_put(dir, pmu);
	return ret;
}

static int test__hwmon_pmu_without_pmu(struct test_suite *test __maybe_unused,
				      int subtest __maybe_unused)
{
	return test__hwmon_pmu(/*with_pmu=*/false);
}

static int test__hwmon_pmu_with_pmu(struct test_suite *test __maybe_unused,
				   int subtest __maybe_unused)
{
	return test__hwmon_pmu(/*with_pmu=*/true);
}

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
	TEST_CASE("Parsing without PMU name", hwmon_pmu_without_pmu),
	TEST_CASE("Parsing with PMU name", hwmon_pmu_with_pmu),
	{	.name = NULL, }
};

struct test_suite suite__hwmon_pmu = {
	.desc = "Hwmon PMU",
	.test_cases = tests__hwmon_pmu,
};
