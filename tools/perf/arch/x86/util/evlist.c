// SPDX-License-Identifier: GPL-2.0
#include <stdio.h>
#include "util/pmu.h"
#include "util/pmus.h"
#include "util/evlist.h"
#include "util/parse-events.h"
#include "util/event.h"
#include "topdown.h"
#include "evsel.h"

static int ___evlist__add_default_attrs(struct evlist *evlist,
					struct perf_event_attr *attrs,
					size_t nr_attrs)
{
	LIST_HEAD(head);
	size_t i = 0;

	for (i = 0; i < nr_attrs; i++)
		event_attr_init(attrs + i);

	if (perf_pmus__num_core_pmus() == 1)
		return evlist__add_attrs(evlist, attrs, nr_attrs);

	for (i = 0; i < nr_attrs; i++) {
		struct perf_pmu *pmu = NULL;

		if (attrs[i].type == PERF_TYPE_SOFTWARE) {
			struct evsel *evsel = evsel__new(attrs + i);

			if (evsel == NULL)
				goto out_delete_partial_list;
			list_add_tail(&evsel->core.node, &head);
			continue;
		}

		while ((pmu = perf_pmus__scan_core(pmu)) != NULL) {
			struct perf_cpu_map *cpus;
			struct evsel *evsel;

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
	{
		struct evsel *evsel, *n;

		__evlist__for_each_entry_safe(&head, n, evsel)
			evsel__delete(evsel);
	}
	return -1;
}

int arch_evlist__add_default_attrs(struct evlist *evlist,
				   struct perf_event_attr *attrs,
				   size_t nr_attrs)
{
	if (!nr_attrs)
		return 0;

	return ___evlist__add_default_attrs(evlist, attrs, nr_attrs);
}

int arch_evlist__cmp(const struct evsel *lhs, const struct evsel *rhs)
{
	if (topdown_sys_has_perf_metrics() && evsel__sys_has_perf_metrics(lhs)) {
		/* Ensure the topdown slots comes first. */
		if (strcasestr(lhs->name, "slots"))
			return -1;
		if (strcasestr(rhs->name, "slots"))
			return 1;
		/* Followed by topdown events. */
		if (strcasestr(lhs->name, "topdown") && !strcasestr(rhs->name, "topdown"))
			return -1;
		if (!strcasestr(lhs->name, "topdown") && strcasestr(rhs->name, "topdown"))
			return 1;
	}

	/* Default ordering by insertion index. */
	return lhs->core.idx - rhs->core.idx;
}
