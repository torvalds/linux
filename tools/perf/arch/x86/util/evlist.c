// SPDX-License-Identifier: GPL-2.0
#include <stdio.h>
#include "util/pmu.h"
#include "util/evlist.h"
#include "util/parse-events.h"
#include "topdown.h"

#define TOPDOWN_L1_EVENTS	"{slots,topdown-retiring,topdown-bad-spec,topdown-fe-bound,topdown-be-bound}"
#define TOPDOWN_L2_EVENTS	"{slots,topdown-retiring,topdown-bad-spec,topdown-fe-bound,topdown-be-bound,topdown-heavy-ops,topdown-br-mispredict,topdown-fetch-lat,topdown-mem-bound}"

int arch_evlist__add_default_attrs(struct evlist *evlist)
{
	if (!pmu_have_event("cpu", "slots"))
		return 0;

	if (pmu_have_event("cpu", "topdown-heavy-ops"))
		return parse_events(evlist, TOPDOWN_L2_EVENTS, NULL);
	else
		return parse_events(evlist, TOPDOWN_L1_EVENTS, NULL);
}

struct evsel *arch_evlist__leader(struct list_head *list)
{
	struct evsel *evsel, *first, *slots = NULL;
	bool has_topdown = false;

	first = list_first_entry(list, struct evsel, core.node);

	if (!topdown_sys_has_perf_metrics())
		return first;

	/* If there is a slots event and a topdown event then the slots event comes first. */
	__evlist__for_each_entry(list, evsel) {
		if (evsel->pmu_name && !strncmp(evsel->pmu_name, "cpu", 3) && evsel->name) {
			if (strcasestr(evsel->name, "slots")) {
				slots = evsel;
				if (slots == first)
					return first;
			}
			if (strcasestr(evsel->name, "topdown"))
				has_topdown = true;
			if (slots && has_topdown)
				return slots;
		}
	}
	return first;
}
