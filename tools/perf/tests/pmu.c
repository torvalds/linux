// SPDX-License-Identifier: GPL-2.0
#include "parse-events.h"
#include "pmu.h"
#include "tests.h"
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <linux/kernel.h>
#include <linux/limits.h>
#include <linux/zalloc.h>

/* Simulated format definitions. */
static struct test_format {
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

/* Simulated users input. */
static struct parse_events_term test_terms[] = {
	{
		.config    = "krava01",
		.val.num   = 15,
		.type_val  = PARSE_EVENTS__TERM_TYPE_NUM,
		.type_term = PARSE_EVENTS__TERM_TYPE_USER,
	},
	{
		.config    = "krava02",
		.val.num   = 170,
		.type_val  = PARSE_EVENTS__TERM_TYPE_NUM,
		.type_term = PARSE_EVENTS__TERM_TYPE_USER,
	},
	{
		.config    = "krava03",
		.val.num   = 1,
		.type_val  = PARSE_EVENTS__TERM_TYPE_NUM,
		.type_term = PARSE_EVENTS__TERM_TYPE_USER,
	},
	{
		.config    = "krava11",
		.val.num   = 27,
		.type_val  = PARSE_EVENTS__TERM_TYPE_NUM,
		.type_term = PARSE_EVENTS__TERM_TYPE_USER,
	},
	{
		.config    = "krava12",
		.val.num   = 1,
		.type_val  = PARSE_EVENTS__TERM_TYPE_NUM,
		.type_term = PARSE_EVENTS__TERM_TYPE_USER,
	},
	{
		.config    = "krava13",
		.val.num   = 2,
		.type_val  = PARSE_EVENTS__TERM_TYPE_NUM,
		.type_term = PARSE_EVENTS__TERM_TYPE_USER,
	},
	{
		.config    = "krava21",
		.val.num   = 119,
		.type_val  = PARSE_EVENTS__TERM_TYPE_NUM,
		.type_term = PARSE_EVENTS__TERM_TYPE_USER,
	},
	{
		.config    = "krava22",
		.val.num   = 11,
		.type_val  = PARSE_EVENTS__TERM_TYPE_NUM,
		.type_term = PARSE_EVENTS__TERM_TYPE_USER,
	},
	{
		.config    = "krava23",
		.val.num   = 2,
		.type_val  = PARSE_EVENTS__TERM_TYPE_NUM,
		.type_term = PARSE_EVENTS__TERM_TYPE_USER,
	},
};

/*
 * Prepare format directory data, exported by kernel
 * at /sys/bus/event_source/devices/<dev>/format.
 */
static char *test_format_dir_get(char *dir, size_t sz)
{
	unsigned int i;

	snprintf(dir, sz, "/tmp/perf-pmu-test-format-XXXXXX");
	if (!mkdtemp(dir))
		return NULL;

	for (i = 0; i < ARRAY_SIZE(test_formats); i++) {
		char name[PATH_MAX];
		struct test_format *format = &test_formats[i];
		FILE *file;

		scnprintf(name, PATH_MAX, "%s/%s", dir, format->name);

		file = fopen(name, "w");
		if (!file)
			return NULL;

		if (1 != fwrite(format->value, strlen(format->value), 1, file))
			break;

		fclose(file);
	}

	return dir;
}

/* Cleanup format directory. */
static int test_format_dir_put(char *dir)
{
	char buf[PATH_MAX + 20];

	snprintf(buf, sizeof(buf), "rm -f %s/*\n", dir);
	if (system(buf))
		return -1;

	snprintf(buf, sizeof(buf), "rmdir %s\n", dir);
	return system(buf);
}

static struct list_head *test_terms_list(void)
{
	static LIST_HEAD(terms);
	unsigned int i;

	for (i = 0; i < ARRAY_SIZE(test_terms); i++)
		list_add_tail(&test_terms[i].list, &terms);

	return &terms;
}

static int test__pmu(struct test_suite *test __maybe_unused, int subtest __maybe_unused)
{
	char dir[PATH_MAX];
	char *format;
	struct list_head *terms = test_terms_list();
	struct perf_event_attr attr;
	struct perf_pmu *pmu;
	int fd;
	int ret;

	pmu = zalloc(sizeof(*pmu));
	if (!pmu)
		return -ENOMEM;

	INIT_LIST_HEAD(&pmu->format);
	INIT_LIST_HEAD(&pmu->aliases);
	INIT_LIST_HEAD(&pmu->caps);
	format = test_format_dir_get(dir, sizeof(dir));
	if (!format) {
		free(pmu);
		return -EINVAL;
	}

	memset(&attr, 0, sizeof(attr));

	fd = open(format, O_DIRECTORY);
	if (fd < 0) {
		ret = fd;
		goto out;
	}

	pmu->name = strdup("perf-pmu-test");
	ret = perf_pmu__format_parse(pmu, fd, /*eager_load=*/true);
	if (ret)
		goto out;

	ret = perf_pmu__config_terms(pmu, &attr, terms, /*zero=*/false, /*err=*/NULL);
	if (ret)
		goto out;

	ret = -EINVAL;
	if (attr.config  != 0xc00000000002a823)
		goto out;
	if (attr.config1 != 0x8000400000000145)
		goto out;
	if (attr.config2 != 0x0400000020041d07)
		goto out;

	ret = 0;
out:
	test_format_dir_put(format);
	perf_pmu__delete(pmu);
	return ret;
}

DEFINE_SUITE("Parse perf pmu format", pmu);
