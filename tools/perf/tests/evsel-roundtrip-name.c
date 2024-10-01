// SPDX-License-Identifier: GPL-2.0
#include "evlist.h"
#include "evsel.h"
#include "parse-events.h"
#include "tests.h"
#include "debug.h"
#include "pmu.h"
#include "pmu-hybrid.h"
#include <errno.h>
#include <linux/kernel.h>

static int perf_evsel__roundtrip_cache_name_test(void)
{
	char name[128];
	int type, op, err = 0, ret = 0, i, idx;
	struct evsel *evsel;
	struct evlist *evlist = evlist__new();

        if (evlist == NULL)
                return -ENOMEM;

	for (type = 0; type < PERF_COUNT_HW_CACHE_MAX; type++) {
		for (op = 0; op < PERF_COUNT_HW_CACHE_OP_MAX; op++) {
			/* skip invalid cache type */
			if (!evsel__is_cache_op_valid(type, op))
				continue;

			for (i = 0; i < PERF_COUNT_HW_CACHE_RESULT_MAX; i++) {
				__evsel__hw_cache_type_op_res_name(type, op, i, name, sizeof(name));
				err = parse_event(evlist, name);
				if (err)
					ret = err;
			}
		}
	}

	idx = 0;
	evsel = evlist__first(evlist);

	for (type = 0; type < PERF_COUNT_HW_CACHE_MAX; type++) {
		for (op = 0; op < PERF_COUNT_HW_CACHE_OP_MAX; op++) {
			/* skip invalid cache type */
			if (!evsel__is_cache_op_valid(type, op))
				continue;

			for (i = 0; i < PERF_COUNT_HW_CACHE_RESULT_MAX; i++) {
				__evsel__hw_cache_type_op_res_name(type, op, i, name, sizeof(name));
				if (evsel->core.idx != idx)
					continue;

				++idx;

				if (strcmp(evsel__name(evsel), name)) {
					pr_debug("%s != %s\n", evsel__name(evsel), name);
					ret = -1;
				}

				evsel = evsel__next(evsel);
			}
		}
	}

	evlist__delete(evlist);
	return ret;
}

static int __perf_evsel__name_array_test(const char *const names[], int nr_names,
					 int distance)
{
	int i, err;
	struct evsel *evsel;
	struct evlist *evlist = evlist__new();

        if (evlist == NULL)
                return -ENOMEM;

	for (i = 0; i < nr_names; ++i) {
		err = parse_event(evlist, names[i]);
		if (err) {
			pr_debug("failed to parse event '%s', err %d\n",
				 names[i], err);
			goto out_delete_evlist;
		}
	}

	err = 0;
	evlist__for_each_entry(evlist, evsel) {
		if (strcmp(evsel__name(evsel), names[evsel->core.idx / distance])) {
			--err;
			pr_debug("%s != %s\n", evsel__name(evsel), names[evsel->core.idx / distance]);
		}
	}

out_delete_evlist:
	evlist__delete(evlist);
	return err;
}

#define perf_evsel__name_array_test(names, distance) \
	__perf_evsel__name_array_test(names, ARRAY_SIZE(names), distance)

static int test__perf_evsel__roundtrip_name_test(struct test_suite *test __maybe_unused,
						 int subtest __maybe_unused)
{
	int err = 0, ret = 0;

	if (perf_pmu__has_hybrid() && perf_pmu__hybrid_mounted("cpu_atom"))
		return perf_evsel__name_array_test(evsel__hw_names, 2);

	err = perf_evsel__name_array_test(evsel__hw_names, 1);
	if (err)
		ret = err;

	err = __perf_evsel__name_array_test(evsel__sw_names, PERF_COUNT_SW_DUMMY + 1, 1);
	if (err)
		ret = err;

	err = perf_evsel__roundtrip_cache_name_test();
	if (err)
		ret = err;

	return ret;
}

DEFINE_SUITE("Roundtrip evsel->name", perf_evsel__roundtrip_name_test);
