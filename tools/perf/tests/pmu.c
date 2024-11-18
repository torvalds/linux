// SPDX-License-Identifier: GPL-2.0
#include "evlist.h"
#include "evsel.h"
#include "parse-events.h"
#include "pmu.h"
#include "pmus.h"
#include "tests.h"
#include "debug.h"
#include "fncache.h"
#include <api/fs/fs.h>
#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/types.h>

/* Cleanup test PMU directory. */
static int test_pmu_put(const char *dir, struct perf_pmu *pmu)
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

	list_del(&pmu->list);
	perf_pmu__delete(pmu);
	return ret;
}

/*
 * Prepare test PMU directory data, normally exported by kernel at
 * /sys/bus/event_source/devices/<pmu>/. Give as input a buffer to hold the file
 * path, the result is PMU loaded using that directory.
 */
static struct perf_pmu *test_pmu_get(char *dir, size_t sz)
{
	/* Simulated format definitions. */
	const struct test_format {
		const char *name;
		const char *value;
	} test_formats[] = {
		{ "krava01", "config:0-1,62-63\n", },
		{ "krava02", "config:10-17\n", },
		{ "krava03", "config:5\n", },
		{ "krava11", "config1:0,2,4,6,8,20-28\n", },
		{ "krava12", "config1:63\n", },
		{ "krava13", "config1:45-47\n", },
		{ "krava21", "config2:0-3,10-13,20-23,30-33,40-43,50-53,60-63\n", },
		{ "krava22", "config2:8,18,48,58\n", },
		{ "krava23", "config2:28-29,38\n", },
	};
	const char *test_event = "krava01=15,krava02=170,krava03=1,krava11=27,krava12=1,"
		"krava13=2,krava21=119,krava22=11,krava23=2\n";

	char name[PATH_MAX];
	int dirfd, file;
	struct perf_pmu *pmu = NULL;
	ssize_t len;

	/* Create equivalent of sysfs mount point. */
	scnprintf(dir, sz, "/tmp/perf-pmu-test-XXXXXX");
	if (!mkdtemp(dir)) {
		pr_err("mkdtemp failed\n");
		dir[0] = '\0';
		return NULL;
	}
	dirfd = open(dir, O_DIRECTORY);
	if (dirfd < 0) {
		pr_err("Failed to open test directory \"%s\"\n", dir);
		goto err_out;
	}

	/* Create the test PMU directory and give it a perf_event_attr type number. */
	if (mkdirat(dirfd, "perf-pmu-test", 0755) < 0) {
		pr_err("Failed to mkdir PMU directory\n");
		goto err_out;
	}
	file = openat(dirfd, "perf-pmu-test/type", O_WRONLY | O_CREAT, 0600);
	if (!file) {
		pr_err("Failed to open for writing file \"type\"\n");
		goto err_out;
	}
	len = strlen("9999");
	if (write(file, "9999\n", len) < len) {
		close(file);
		pr_err("Failed to write to 'type' file\n");
		goto err_out;
	}
	close(file);

	/* Create format directory and files. */
	if (mkdirat(dirfd, "perf-pmu-test/format", 0755) < 0) {
		pr_err("Failed to mkdir PMU format directory\n)");
		goto err_out;
	}
	for (size_t i = 0; i < ARRAY_SIZE(test_formats); i++) {
		const struct test_format *format = &test_formats[i];

		if (scnprintf(name, PATH_MAX, "perf-pmu-test/format/%s", format->name) < 0) {
			pr_err("Failure to set up path for \"%s\"\n", format->name);
			goto err_out;
		}
		file = openat(dirfd, name, O_WRONLY | O_CREAT, 0600);
		if (!file) {
			pr_err("Failed to open for writing file \"%s\"\n", name);
			goto err_out;
		}

		if (write(file, format->value, strlen(format->value)) < 0) {
			pr_err("Failed to write to file \"%s\"\n", name);
			close(file);
			goto err_out;
		}
		close(file);
	}

	/* Create test event. */
	if (mkdirat(dirfd, "perf-pmu-test/events", 0755) < 0) {
		pr_err("Failed to mkdir PMU events directory\n");
		goto err_out;
	}
	file = openat(dirfd, "perf-pmu-test/events/test-event", O_WRONLY | O_CREAT, 0600);
	if (!file) {
		pr_err("Failed to open for writing file \"type\"\n");
		goto err_out;
	}
	len = strlen(test_event);
	if (write(file, test_event, len) < len) {
		close(file);
		pr_err("Failed to write to 'test-event' file\n");
		goto err_out;
	}
	close(file);

	/* Make the PMU reading the files created above. */
	pmu = perf_pmus__add_test_pmu(dirfd, "perf-pmu-test");
	if (!pmu)
		pr_err("Test PMU creation failed\n");

err_out:
	if (!pmu)
		test_pmu_put(dir, pmu);
	if (dirfd >= 0)
		close(dirfd);
	return pmu;
}

static int test__pmu_format(struct test_suite *test __maybe_unused, int subtest __maybe_unused)
{
	char dir[PATH_MAX];
	struct perf_event_attr attr;
	struct parse_events_terms terms;
	int ret = TEST_FAIL;
	struct perf_pmu *pmu = test_pmu_get(dir, sizeof(dir));

	if (!pmu)
		return TEST_FAIL;

	parse_events_terms__init(&terms);
	if (parse_events_terms(&terms,
				"krava01=15,krava02=170,krava03=1,krava11=27,krava12=1,"
				"krava13=2,krava21=119,krava22=11,krava23=2",
				NULL)) {
		pr_err("Term parsing failed\n");
		goto err_out;
	}

	memset(&attr, 0, sizeof(attr));
	ret = perf_pmu__config_terms(pmu, &attr, &terms, /*zero=*/false, /*err=*/NULL);
	if (ret) {
		pr_err("perf_pmu__config_terms failed");
		goto err_out;
	}

	if (attr.config  != 0xc00000000002a823) {
		pr_err("Unexpected config value %llx\n", attr.config);
		goto err_out;
	}
	if (attr.config1 != 0x8000400000000145) {
		pr_err("Unexpected config1 value %llx\n", attr.config1);
		goto err_out;
	}
	if (attr.config2 != 0x0400000020041d07) {
		pr_err("Unexpected config2 value %llx\n", attr.config2);
		goto err_out;
	}

	ret = TEST_OK;
err_out:
	parse_events_terms__exit(&terms);
	test_pmu_put(dir, pmu);
	return ret;
}

static int test__pmu_events(struct test_suite *test __maybe_unused, int subtest __maybe_unused)
{
	char dir[PATH_MAX];
	struct parse_events_error err;
	struct evlist *evlist;
	struct evsel *evsel;
	struct perf_event_attr *attr;
	int ret = TEST_FAIL;
	struct perf_pmu *pmu = test_pmu_get(dir, sizeof(dir));
	const char *event = "perf-pmu-test/test-event/";


	if (!pmu)
		return TEST_FAIL;

	evlist = evlist__new();
	if (evlist == NULL) {
		pr_err("Failed allocation");
		goto err_out;
	}
	parse_events_error__init(&err);
	ret = parse_events(evlist, event, &err);
	if (ret) {
		pr_debug("failed to parse event '%s', err %d\n", event, ret);
		parse_events_error__print(&err, event);
		if (parse_events_error__contains(&err, "can't access trace events"))
			ret = TEST_SKIP;
		goto err_out;
	}
	evsel = evlist__first(evlist);
	attr = &evsel->core.attr;
	if (attr->config  != 0xc00000000002a823) {
		pr_err("Unexpected config value %llx\n", attr->config);
		goto err_out;
	}
	if (attr->config1 != 0x8000400000000145) {
		pr_err("Unexpected config1 value %llx\n", attr->config1);
		goto err_out;
	}
	if (attr->config2 != 0x0400000020041d07) {
		pr_err("Unexpected config2 value %llx\n", attr->config2);
		goto err_out;
	}

	ret = TEST_OK;
err_out:
	parse_events_error__exit(&err);
	evlist__delete(evlist);
	test_pmu_put(dir, pmu);
	return ret;
}

static bool permitted_event_name(const char *name)
{
	bool has_lower = false, has_upper = false;
	__u64 config;

	for (size_t i = 0; i < strlen(name); i++) {
		char c = name[i];

		if (islower(c)) {
			if (has_upper)
				goto check_legacy;
			has_lower = true;
			continue;
		}
		if (isupper(c)) {
			if (has_lower)
				goto check_legacy;
			has_upper = true;
			continue;
		}
		if (!isdigit(c) && c != '.' && c != '_' && c != '-')
			goto check_legacy;
	}
	return true;
check_legacy:
	/*
	 * If the event name matches a legacy cache name the legacy encoding
	 * will still be used. This isn't quite WAI as sysfs events should take
	 * priority, but this case happens on PowerPC and matches the behavior
	 * in older perf tools where legacy events were the priority. Be
	 * permissive and assume later PMU drivers will use all lower or upper
	 * case names.
	 */
	if (parse_events__decode_legacy_cache(name, /*extended_pmu_type=*/0, &config) == 0) {
		pr_warning("sysfs event '%s' should be all lower/upper case, it will be matched using legacy encoding.",
			   name);
		return true;
	}
	return false;
}

static int test__pmu_event_names(struct test_suite *test __maybe_unused,
				 int subtest __maybe_unused)
{
	char path[PATH_MAX];
	DIR *pmu_dir, *event_dir;
	struct dirent *pmu_dent, *event_dent;
	const char *sysfs = sysfs__mountpoint();
	int ret = TEST_OK;

	if (!sysfs) {
		pr_err("Sysfs not mounted\n");
		return TEST_FAIL;
	}

	snprintf(path, sizeof(path), "%s/bus/event_source/devices/", sysfs);
	pmu_dir = opendir(path);
	if (!pmu_dir) {
		pr_err("Error opening \"%s\"\n", path);
		return TEST_FAIL;
	}
	while ((pmu_dent = readdir(pmu_dir))) {
		if (!strcmp(pmu_dent->d_name, ".") ||
		    !strcmp(pmu_dent->d_name, ".."))
			continue;

		snprintf(path, sizeof(path), "%s/bus/event_source/devices/%s/type",
			 sysfs, pmu_dent->d_name);

		/* Does it look like a PMU? */
		if (!file_available(path))
			continue;

		/* Process events. */
		snprintf(path, sizeof(path), "%s/bus/event_source/devices/%s/events",
			 sysfs, pmu_dent->d_name);

		event_dir = opendir(path);
		if (!event_dir) {
			pr_debug("Skipping as no event directory \"%s\"\n", path);
			continue;
		}
		while ((event_dent = readdir(event_dir))) {
			const char *event_name = event_dent->d_name;

			if (!strcmp(event_name, ".") || !strcmp(event_name, ".."))
				continue;

			if (!permitted_event_name(event_name)) {
				pr_err("Invalid sysfs event name: %s/%s\n",
					pmu_dent->d_name, event_name);
				ret = TEST_FAIL;
			}
		}
		closedir(event_dir);
	}
	closedir(pmu_dir);
	return ret;
}

static const char * const uncore_chas[] = {
	"uncore_cha_0",
	"uncore_cha_1",
	"uncore_cha_2",
	"uncore_cha_3",
	"uncore_cha_4",
	"uncore_cha_5",
	"uncore_cha_6",
	"uncore_cha_7",
	"uncore_cha_8",
	"uncore_cha_9",
	"uncore_cha_10",
	"uncore_cha_11",
	"uncore_cha_12",
	"uncore_cha_13",
	"uncore_cha_14",
	"uncore_cha_15",
	"uncore_cha_16",
	"uncore_cha_17",
	"uncore_cha_18",
	"uncore_cha_19",
	"uncore_cha_20",
	"uncore_cha_21",
	"uncore_cha_22",
	"uncore_cha_23",
	"uncore_cha_24",
	"uncore_cha_25",
	"uncore_cha_26",
	"uncore_cha_27",
	"uncore_cha_28",
	"uncore_cha_29",
	"uncore_cha_30",
	"uncore_cha_31",
};

static const char * const mrvl_ddrs[] = {
	"mrvl_ddr_pmu_87e1b0000000",
	"mrvl_ddr_pmu_87e1b1000000",
	"mrvl_ddr_pmu_87e1b2000000",
	"mrvl_ddr_pmu_87e1b3000000",
	"mrvl_ddr_pmu_87e1b4000000",
	"mrvl_ddr_pmu_87e1b5000000",
	"mrvl_ddr_pmu_87e1b6000000",
	"mrvl_ddr_pmu_87e1b7000000",
	"mrvl_ddr_pmu_87e1b8000000",
	"mrvl_ddr_pmu_87e1b9000000",
	"mrvl_ddr_pmu_87e1ba000000",
	"mrvl_ddr_pmu_87e1bb000000",
	"mrvl_ddr_pmu_87e1bc000000",
	"mrvl_ddr_pmu_87e1bd000000",
	"mrvl_ddr_pmu_87e1be000000",
	"mrvl_ddr_pmu_87e1bf000000",
};

static int test__name_len(struct test_suite *test __maybe_unused, int subtest __maybe_unused)
{
	TEST_ASSERT_VAL("cpu", pmu_name_len_no_suffix("cpu") == strlen("cpu"));
	TEST_ASSERT_VAL("i915", pmu_name_len_no_suffix("i915") == strlen("i915"));
	TEST_ASSERT_VAL("cpum_cf", pmu_name_len_no_suffix("cpum_cf") == strlen("cpum_cf"));
	for (size_t i = 0; i < ARRAY_SIZE(uncore_chas); i++) {
		TEST_ASSERT_VAL("Strips uncore_cha suffix",
				pmu_name_len_no_suffix(uncore_chas[i]) ==
				strlen("uncore_cha"));
	}
	for (size_t i = 0; i < ARRAY_SIZE(mrvl_ddrs); i++) {
		TEST_ASSERT_VAL("Strips mrvl_ddr_pmu suffix",
				pmu_name_len_no_suffix(mrvl_ddrs[i]) ==
				strlen("mrvl_ddr_pmu"));
	}
	return TEST_OK;
}

static int test__name_cmp(struct test_suite *test __maybe_unused, int subtest __maybe_unused)
{
	TEST_ASSERT_EQUAL("cpu", pmu_name_cmp("cpu", "cpu"), 0);
	TEST_ASSERT_EQUAL("i915", pmu_name_cmp("i915", "i915"), 0);
	TEST_ASSERT_EQUAL("cpum_cf", pmu_name_cmp("cpum_cf", "cpum_cf"), 0);
	TEST_ASSERT_VAL("i915", pmu_name_cmp("cpu", "i915") < 0);
	TEST_ASSERT_VAL("i915", pmu_name_cmp("i915", "cpu") > 0);
	TEST_ASSERT_VAL("cpum_cf", pmu_name_cmp("cpum_cf", "cpum_ce") > 0);
	TEST_ASSERT_VAL("cpum_cf", pmu_name_cmp("cpum_cf", "cpum_d0") < 0);
	for (size_t i = 1; i < ARRAY_SIZE(uncore_chas); i++) {
		TEST_ASSERT_VAL("uncore_cha suffixes ordered lt",
				pmu_name_cmp(uncore_chas[i-1], uncore_chas[i]) < 0);
		TEST_ASSERT_VAL("uncore_cha suffixes ordered gt",
				pmu_name_cmp(uncore_chas[i], uncore_chas[i-1]) > 0);
	}
	for (size_t i = 1; i < ARRAY_SIZE(mrvl_ddrs); i++) {
		TEST_ASSERT_VAL("mrvl_ddr_pmu suffixes ordered lt",
				pmu_name_cmp(mrvl_ddrs[i-1], mrvl_ddrs[i]) < 0);
		TEST_ASSERT_VAL("mrvl_ddr_pmu suffixes ordered gt",
				pmu_name_cmp(mrvl_ddrs[i], mrvl_ddrs[i-1]) > 0);
	}
	return TEST_OK;
}

/**
 * Test perf_pmu__match() that's used to search for a PMU given a name passed
 * on the command line. The name that's passed may also be a filename type glob
 * match. If the name does not match, perf_pmu__match() attempts to match the
 * alias of the PMU, if provided.
 */
static int test__pmu_match(struct test_suite *test __maybe_unused, int subtest __maybe_unused)
{
	struct perf_pmu test_pmu = {
		.name = "pmuname",
	};

	TEST_ASSERT_EQUAL("Exact match", perf_pmu__match(&test_pmu, "pmuname"),	     true);
	TEST_ASSERT_EQUAL("Longer token", perf_pmu__match(&test_pmu, "longertoken"), false);
	TEST_ASSERT_EQUAL("Shorter token", perf_pmu__match(&test_pmu, "pmu"),	     false);

	test_pmu.name = "pmuname_10";
	TEST_ASSERT_EQUAL("Diff suffix_", perf_pmu__match(&test_pmu, "pmuname_2"),  false);
	TEST_ASSERT_EQUAL("Sub suffix_",  perf_pmu__match(&test_pmu, "pmuname_1"),  true);
	TEST_ASSERT_EQUAL("Same suffix_", perf_pmu__match(&test_pmu, "pmuname_10"), true);
	TEST_ASSERT_EQUAL("No suffix_",   perf_pmu__match(&test_pmu, "pmuname"),    true);
	TEST_ASSERT_EQUAL("Underscore_",  perf_pmu__match(&test_pmu, "pmuname_"),   true);
	TEST_ASSERT_EQUAL("Substring_",   perf_pmu__match(&test_pmu, "pmuna"),      false);

	test_pmu.name = "pmuname_ab23";
	TEST_ASSERT_EQUAL("Diff suffix hex_", perf_pmu__match(&test_pmu, "pmuname_2"),    false);
	TEST_ASSERT_EQUAL("Sub suffix hex_",  perf_pmu__match(&test_pmu, "pmuname_ab"),   true);
	TEST_ASSERT_EQUAL("Same suffix hex_", perf_pmu__match(&test_pmu, "pmuname_ab23"), true);
	TEST_ASSERT_EQUAL("No suffix hex_",   perf_pmu__match(&test_pmu, "pmuname"),      true);
	TEST_ASSERT_EQUAL("Underscore hex_",  perf_pmu__match(&test_pmu, "pmuname_"),     true);
	TEST_ASSERT_EQUAL("Substring hex_",   perf_pmu__match(&test_pmu, "pmuna"),	 false);

	test_pmu.name = "pmuname10";
	TEST_ASSERT_EQUAL("Diff suffix", perf_pmu__match(&test_pmu, "pmuname2"),  false);
	TEST_ASSERT_EQUAL("Sub suffix",  perf_pmu__match(&test_pmu, "pmuname1"),  true);
	TEST_ASSERT_EQUAL("Same suffix", perf_pmu__match(&test_pmu, "pmuname10"), true);
	TEST_ASSERT_EQUAL("No suffix",   perf_pmu__match(&test_pmu, "pmuname"),   true);
	TEST_ASSERT_EQUAL("Underscore",  perf_pmu__match(&test_pmu, "pmuname_"),  false);
	TEST_ASSERT_EQUAL("Substring",   perf_pmu__match(&test_pmu, "pmuna"),     false);

	test_pmu.name = "pmunameab23";
	TEST_ASSERT_EQUAL("Diff suffix hex", perf_pmu__match(&test_pmu, "pmuname2"),    false);
	TEST_ASSERT_EQUAL("Sub suffix hex",  perf_pmu__match(&test_pmu, "pmunameab"),   true);
	TEST_ASSERT_EQUAL("Same suffix hex", perf_pmu__match(&test_pmu, "pmunameab23"), true);
	TEST_ASSERT_EQUAL("No suffix hex",   perf_pmu__match(&test_pmu, "pmuname"),     true);
	TEST_ASSERT_EQUAL("Underscore hex",  perf_pmu__match(&test_pmu, "pmuname_"),    false);
	TEST_ASSERT_EQUAL("Substring hex",   perf_pmu__match(&test_pmu, "pmuna"),	false);

	/*
	 * 2 hex chars or less are not considered suffixes so it shouldn't be
	 * possible to wildcard by skipping the suffix. Therefore there are more
	 * false results here than above.
	 */
	test_pmu.name = "pmuname_a3";
	TEST_ASSERT_EQUAL("Diff suffix 2 hex_", perf_pmu__match(&test_pmu, "pmuname_2"),  false);
	/*
	 * This one should be false, but because pmuname_a3 ends in 3 which is
	 * decimal, it's not possible to determine if it's a short hex suffix or
	 * a normal decimal suffix following text. And we want to match on any
	 * length of decimal suffix. Run the test anyway and expect the wrong
	 * result. And slightly fuzzy matching shouldn't do too much harm.
	 */
	TEST_ASSERT_EQUAL("Sub suffix 2 hex_",  perf_pmu__match(&test_pmu, "pmuname_a"),  true);
	TEST_ASSERT_EQUAL("Same suffix 2 hex_", perf_pmu__match(&test_pmu, "pmuname_a3"), true);
	TEST_ASSERT_EQUAL("No suffix 2 hex_",   perf_pmu__match(&test_pmu, "pmuname"),    false);
	TEST_ASSERT_EQUAL("Underscore 2 hex_",  perf_pmu__match(&test_pmu, "pmuname_"),   false);
	TEST_ASSERT_EQUAL("Substring 2 hex_",   perf_pmu__match(&test_pmu, "pmuna"),	  false);

	test_pmu.name = "pmuname_5";
	TEST_ASSERT_EQUAL("Glob 1", perf_pmu__match(&test_pmu, "pmu*"),		   true);
	TEST_ASSERT_EQUAL("Glob 2", perf_pmu__match(&test_pmu, "nomatch*"),	   false);
	TEST_ASSERT_EQUAL("Seq 1",  perf_pmu__match(&test_pmu, "pmuname_[12345]"), true);
	TEST_ASSERT_EQUAL("Seq 2",  perf_pmu__match(&test_pmu, "pmuname_[67890]"), false);
	TEST_ASSERT_EQUAL("? 1",    perf_pmu__match(&test_pmu, "pmuname_?"),	   true);
	TEST_ASSERT_EQUAL("? 2",    perf_pmu__match(&test_pmu, "pmuname_1?"),	   false);

	return TEST_OK;
}

static struct test_case tests__pmu[] = {
	TEST_CASE("Parsing with PMU format directory", pmu_format),
	TEST_CASE("Parsing with PMU event", pmu_events),
	TEST_CASE("PMU event names", pmu_event_names),
	TEST_CASE("PMU name combining", name_len),
	TEST_CASE("PMU name comparison", name_cmp),
	TEST_CASE("PMU cmdline match", pmu_match),
	{	.name = NULL, }
};

struct test_suite suite__pmu = {
	.desc = "Sysfs PMU tests",
	.test_cases = tests__pmu,
};
