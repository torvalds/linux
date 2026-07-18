// SPDX-License-Identifier: (LGPL-2.1 OR BSD-2-Clause)
#include <string.h>

#include "debug.h"
#include "evlist.h"
#include "parse-events.h"
#include "pmu.h"
#include "pmus.h"
#include "tests.h"

struct match_state {
	char *event1;
	char *event2;
};

static char *clean_event_name(struct pmu_event_info *info)
{
	const char *name = info->name;
	const char *pmu_name = info->pmu->name;
	size_t pmu_len = strlen(pmu_name);
	char *res;
	size_t len;

	if (!strncmp(name, pmu_name, pmu_len) && name[pmu_len] == '/')
		name += pmu_len + 1;

	res = strdup(name);
	if (!res)
		return NULL;

	len = strlen(res);
	if (len > 0 && res[len - 1] == '/')
		res[len - 1] = '\0';

	return res;
}

static int event_cb(void *state, struct pmu_event_info *info)
{
	struct match_state *m = state;
	char *clean_name;

	if (m->event1 && m->event2)
		return 1;

	clean_name = clean_event_name(info);
	if (!clean_name)
		return 0;

	if (!m->event1) {
		m->event1 = clean_name;
	} else {
		if (strcmp(m->event1, clean_name)) {
			m->event2 = clean_name;
			return 1;
		}
		free(clean_name);
	}
	return 0;
}

#define CHECK_COND(cond, text)					\
do {								\
	if (!(cond)) {						\
		pr_debug("FAILED %s:%d %s\n", __FILE__, __LINE__, text); \
		ret = TEST_FAIL;				\
		goto out_err;					\
	}							\
} while (0)

#define CHECK_EQUAL(val, expected, text)			\
do {								\
	if ((val) != (expected)) {				\
		pr_debug("FAILED %s:%d %s (%d != %d)\n",	\
			 __FILE__, __LINE__, text, (val), (expected)); \
		ret = TEST_FAIL;				\
		goto out_err;					\
	}							\
} while (0)

static int test__uncore_event_sorting(struct test_suite *test __maybe_unused,
				      int subtest __maybe_unused)
{
	struct evlist *evlist = NULL;
	struct parse_events_error err;
	struct evsel *evsel;
	struct perf_pmu *pmu = NULL;
	char *pmu_prefix = NULL;
	struct match_state m = { NULL, NULL };
	char buf[1024];
	int ret;

	parse_events_error__init(&err);

	while ((pmu = perf_pmus__scan(pmu)) != NULL) {
		size_t len;
		struct perf_pmu *sibling;

		if (pmu->is_core)
			continue;

		len = pmu_name_len_no_suffix(pmu->name);
		if (len == strlen(pmu->name))
			continue;

		sibling = pmu;
		while ((sibling = perf_pmus__scan(sibling)) != NULL) {
			if (sibling->is_core)
				continue;
			if (pmu_name_len_no_suffix(sibling->name) == len &&
			    !strncmp(pmu->name, sibling->name, len))
				break;
		}

		if (!sibling)
			continue;

		m.event1 = m.event2 = NULL;
		perf_pmu__for_each_event(pmu, false, &m, event_cb);

		if (m.event1 && m.event2) {
			pmu_prefix = strndup(pmu->name, len);
			break;
		}
		zfree(&m.event1);
	}

	if (!pmu_prefix) {
		pr_debug("No suitable uncore PMU found\n");
		ret = TEST_SKIP;
		goto out_err;
	}

	evlist = evlist__new();
	if (!evlist) {
		ret = TEST_FAIL;
		goto out_err;
	}

	snprintf(buf, sizeof(buf), "{%s/%s/,%s/%s/}", pmu_prefix, m.event1, pmu_prefix, m.event2);
	pr_debug("Parsing: %s\n", buf);

	ret = parse_events(evlist, buf, &err);
	if (ret) {
		pr_debug("parse_events failed\n");
		ret = TEST_FAIL;
		goto out_err;
	}

	CHECK_COND(evlist->core.nr_entries >= 4, "Number of events is >= 4");
	CHECK_EQUAL(evlist->core.nr_entries % 2, 0, "Number of events is a multiple of 2");

	evlist__for_each_entry(evlist, evsel) {
		struct evsel *next;

		if (!evsel__is_group_leader(evsel))
			continue;

		next = evsel__next(evsel);
		CHECK_EQUAL(evsel->core.nr_members, 2, "Group size is 2");
		CHECK_COND(evsel->pmu == next->pmu, "PMU match");
		CHECK_COND(strstr(evsel__name(evsel), m.event1) != NULL, "First event name");
		CHECK_COND(strstr(evsel__name(next), m.event2) != NULL, "Second event name");
	}
	ret = TEST_OK;

out_err:
	evlist__delete(evlist);
	parse_events_error__exit(&err);
	zfree(&pmu_prefix);
	zfree(&m.event1);
	zfree(&m.event2);
	return ret;
}

DEFINE_SUITE("Uncore event sorting", uncore_event_sorting);
