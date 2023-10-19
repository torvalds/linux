// SPDX-License-Identifier: GPL-2.0
#include <stdio.h>
#include "util/pmu.h"
#include "util/evlist.h"
#include "util/parse-events.h"
#include "util/event.h"
#include "util/pmu-hybrid.h"
#include "topdown.h"

static int ___evlist__add_default_attrs(struct evlist *evlist,
					struct perf_event_attr *attrs,
					size_t nr_attrs)
{
	struct perf_cpu_map *cpus;
	struct evsel *evsel, *n;
	struct perf_pmu *pmu;
	LIST_HEAD(head);
	size_t i = 0;

	for (i = 0; i < nr_attrs; i++)
		event_attr_init(attrs + i);

	if (!perf_pmu__has_hybrid())
		return evlist__add_attrs(evlist, attrs, nr_attrs);

	for (i = 0; i < nr_attrs; i++) {
		if (attrs[i].type == PERF_TYPE_SOFTWARE) {
			evsel = evsel__new(attrs + i);
			if (evsel == NULL)
				goto out_delete_partial_list;
			list_add_tail(&evsel->core.node, &head);
			continue;
		}

		perf_pmu__for_each_hybrid_pmu(pmu) {
			evsel = evsel__new(attrs + i);
			if (evsel == NULL)
				goto out_delete_partial_list;
			evsel->core.attr.config |= (__u64)pmu->type << PERF_PMU_TYPE_SHIFT;
			cpus = perf_cpu_map__get(pmu->cpus);
			evsel->core.cpus = cpus;
			evsel->core.own_cpus = perf_cpu_map__get(cpus);
			evsel->pmu_name = strdup(pmu->name);
			list_add_tail(&evsel->core.node, &head);
		}
	}

	evlist__splice_list_tail(evlist, &head);

	return 0;

out_delete_partial_list:
	__evlist__for_each_entry_safe(&head, n, evsel)
		evsel__delete(evsel);
	return -1;
}

int arch_evlist__add_default_attrs(struct evlist *evlist,
				   struct perf_event_attr *attrs,
				   size_t nr_attrs)
{
	if (nr_attrs)
		return ___evlist__add_default_attrs(evlist, attrs, nr_attrs);

	return topdown_parse_events(evlist);
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
