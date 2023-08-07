// SPDX-License-Identifier: GPL-2.0
#include "evlist.h"
#include "evsel.h"
#include "parse-events.h"
#include "tests.h"
#include "debug.h"
#include <linux/kernel.h>

static int perf_evsel__roundtrip_cache_name_test(void)
{
	int ret = TEST_OK;

	for (int type = 0; type < PERF_COUNT_HW_CACHE_MAX; type++) {
		for (int op = 0; op < PERF_COUNT_HW_CACHE_OP_MAX; op++) {
			/* skip invalid cache type */
			if (!evsel__is_cache_op_valid(type, op))
				continue;

			for (int res = 0; res < PERF_COUNT_HW_CACHE_RESULT_MAX; res++) {
				char name[128];
				struct evlist *evlist = evlist__new();
				struct evsel *evsel;
				int err;

				if (evlist == NULL) {
					pr_debug("Failed to alloc evlist");
					return TEST_FAIL;
				}
				__evsel__hw_cache_type_op_res_name(type, op, res,
								name, sizeof(name));

				err = parse_event(evlist, name);
				if (err) {
					pr_debug("Failure to parse cache event '%s' possibly as PMUs don't support it",
						name);
					evlist__delete(evlist);
					continue;
				}
				evlist__for_each_entry(evlist, evsel) {
					if (strcmp(evsel__name(evsel), name)) {
						pr_debug("%s != %s\n", evsel__name(evsel), name);
						ret = TEST_FAIL;
					}
				}
				evlist__delete(evlist);
			}
		}
	}
	return ret;
}

static int perf_evsel__name_array_test(const char *const names[], int nr_names)
{
	int ret = TEST_OK;

	for (int i = 0; i < nr_names; ++i) {
		struct evlist *evlist = evlist__new();
		struct evsel *evsel;
		int err;

		if (evlist == NULL) {
			pr_debug("Failed to alloc evlist");
			return TEST_FAIL;
		}
		err = parse_event(evlist, names[i]);
		if (err) {
			pr_debug("failed to parse event '%s', err %d\n",
				 names[i], err);
			evlist__delete(evlist);
			ret = TEST_FAIL;
			continue;
		}
		evlist__for_each_entry(evlist, evsel) {
			if (strcmp(evsel__name(evsel), names[i])) {
				pr_debug("%s != %s\n", evsel__name(evsel), names[i]);
				ret = TEST_FAIL;
			}
		}
		evlist__delete(evlist);
	}
	return ret;
}

static int test__perf_evsel__roundtrip_name_test(struct test_suite *test __maybe_unused,
						 int subtest __maybe_unused)
{
	int err = 0, ret = TEST_OK;

	err = perf_evsel__name_array_test(evsel__hw_names, PERF_COUNT_HW_MAX);
	if (err)
		ret = err;

	err = perf_evsel__name_array_test(evsel__sw_names, PERF_COUNT_SW_DUMMY + 1);
	if (err)
		ret = err;

	err = perf_evsel__roundtrip_cache_name_test();
	if (err)
		ret = err;

	return ret;
}

DEFINE_SUITE("Roundtrip evsel->name", perf_evsel__roundtrip_name_test);
