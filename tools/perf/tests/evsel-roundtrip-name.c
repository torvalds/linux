#include "evlist.h"
#include "evsel.h"
#include "parse-events.h"
#include "tests.h"

static int perf_evsel__roundtrip_cache_name_test(void)
{
	char name[128];
	int type, op, err = 0, ret = 0, i, idx;
	struct perf_evsel *evsel;
        struct perf_evlist *evlist = perf_evlist__new(NULL, NULL);

        if (evlist == NULL)
                return -ENOMEM;

	for (type = 0; type < PERF_COUNT_HW_CACHE_MAX; type++) {
		for (op = 0; op < PERF_COUNT_HW_CACHE_OP_MAX; op++) {
			/* skip invalid cache type */
			if (!perf_evsel__is_cache_op_valid(type, op))
				continue;

			for (i = 0; i < PERF_COUNT_HW_CACHE_RESULT_MAX; i++) {
				__perf_evsel__hw_cache_type_op_res_name(type, op, i,
									name, sizeof(name));
				err = parse_events(evlist, name);
				if (err)
					ret = err;
			}
		}
	}

	idx = 0;
	evsel = perf_evlist__first(evlist);

	for (type = 0; type < PERF_COUNT_HW_CACHE_MAX; type++) {
		for (op = 0; op < PERF_COUNT_HW_CACHE_OP_MAX; op++) {
			/* skip invalid cache type */
			if (!perf_evsel__is_cache_op_valid(type, op))
				continue;

			for (i = 0; i < PERF_COUNT_HW_CACHE_RESULT_MAX; i++) {
				__perf_evsel__hw_cache_type_op_res_name(type, op, i,
									name, sizeof(name));
				if (evsel->idx != idx)
					continue;

				++idx;

				if (strcmp(perf_evsel__name(evsel), name)) {
					pr_debug("%s != %s\n", perf_evsel__name(evsel), name);
					ret = -1;
				}

				evsel = perf_evsel__next(evsel);
			}
		}
	}

	perf_evlist__delete(evlist);
	return ret;
}

static int __perf_evsel__name_array_test(const char *names[], int nr_names)
{
	int i, err;
	struct perf_evsel *evsel;
        struct perf_evlist *evlist = perf_evlist__new(NULL, NULL);

        if (evlist == NULL)
                return -ENOMEM;

	for (i = 0; i < nr_names; ++i) {
		err = parse_events(evlist, names[i]);
		if (err) {
			pr_debug("failed to parse event '%s', err %d\n",
				 names[i], err);
			goto out_delete_evlist;
		}
	}

	err = 0;
	list_for_each_entry(evsel, &evlist->entries, node) {
		if (strcmp(perf_evsel__name(evsel), names[evsel->idx])) {
			--err;
			pr_debug("%s != %s\n", perf_evsel__name(evsel), names[evsel->idx]);
		}
	}

out_delete_evlist:
	perf_evlist__delete(evlist);
	return err;
}

#define perf_evsel__name_array_test(names) \
	__perf_evsel__name_array_test(names, ARRAY_SIZE(names))

int test__perf_evsel__roundtrip_name_test(void)
{
	int err = 0, ret = 0;

	err = perf_evsel__name_array_test(perf_evsel__hw_names);
	if (err)
		ret = err;

	err = perf_evsel__name_array_test(perf_evsel__sw_names);
	if (err)
		ret = err;

	err = perf_evsel__roundtrip_cache_name_test();
	if (err)
		ret = err;

	return ret;
}
