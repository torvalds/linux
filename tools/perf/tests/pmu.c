// SPDX-License-Identifier: GPL-2.0
#include "evlist.h"
#include "evsel.h"
#include "parse-events.h"
#include "pmu.h"
#include "tests.h"
#include "debug.h"
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/stat.h>

/* Fake PMUs created in temp directory. */
static LIST_HEAD(test_pmus);

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

static struct test_case tests__pmu[] = {
	TEST_CASE("Parsing with PMU format directory", pmu_format),
	TEST_CASE("Parsing with PMU event", pmu_events),
	{	.name = NULL, }
};

struct test_suite suite__pmu = {
	.desc = "Sysfs PMU tests",
	.test_cases = tests__pmu,
};
