// SPDX-License-Identifier: GPL-2.0
#include "arch-tests.h"
#include "../util/topdown.h"
#include "debug.h"
#include "evlist.h"
#include "parse-events.h"
#include "pmu.h"
#include "pmus.h"

static int event_cb(void *state, struct pmu_event_info *info)
{
	char buf[256];
	struct parse_events_error parse_err;
	int *ret = state, err;
	struct evlist *evlist = evlist__new();
	struct evsel *evsel;

	if (!evlist)
		return -ENOMEM;

	parse_events_error__init(&parse_err);
	snprintf(buf, sizeof(buf), "%s/%s/", info->pmu->name, info->name);
	err = parse_events(evlist, buf, &parse_err);
	if (err) {
		parse_events_error__print(&parse_err, buf);
		*ret = TEST_FAIL;
	}
	parse_events_error__exit(&parse_err);
	evlist__for_each_entry(evlist, evsel) {
		bool fail = false;
		bool p_core_pmu = evsel->pmu->type == PERF_TYPE_RAW;
		const char *name = evsel__name(evsel);

		if (strcasestr(name, "uops_retired.slots") ||
		    strcasestr(name, "topdown.backend_bound_slots") ||
		    strcasestr(name, "topdown.br_mispredict_slots") ||
		    strcasestr(name, "topdown.memory_bound_slots") ||
		    strcasestr(name, "topdown.bad_spec_slots") ||
		    strcasestr(name, "topdown.slots_p")) {
			if (arch_is_topdown_slots(evsel) || arch_is_topdown_metrics(evsel))
				fail = true;
		} else if (strcasestr(name, "slots")) {
			if (arch_is_topdown_slots(evsel) != p_core_pmu ||
			    arch_is_topdown_metrics(evsel))
				fail = true;
		} else if (strcasestr(name, "topdown")) {
			if (arch_is_topdown_slots(evsel) ||
			    arch_is_topdown_metrics(evsel) != p_core_pmu)
				fail = true;
		} else if (arch_is_topdown_slots(evsel) || arch_is_topdown_metrics(evsel)) {
			fail = true;
		}
		if (fail) {
			pr_debug("Broken topdown information for '%s'\n", evsel__name(evsel));
			*ret = TEST_FAIL;
		}
	}
	evlist__delete(evlist);
	return 0;
}

static int test__x86_topdown(struct test_suite *test __maybe_unused, int subtest __maybe_unused)
{
	int ret = TEST_OK;
	struct perf_pmu *pmu = NULL;

	if (!topdown_sys_has_perf_metrics())
		return TEST_OK;

	while ((pmu = perf_pmus__scan_core(pmu)) != NULL) {
		if (perf_pmu__for_each_event(pmu, /*skip_duplicate_pmus=*/false, &ret, event_cb))
			break;
	}
	return ret;
}

DEFINE_SUITE("x86 topdown", x86_topdown);
