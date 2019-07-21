// SPDX-License-Identifier: GPL-2.0
#include <perf/evlist.h>
#include <linux/list.h>
#include <internal/evlist.h>
#include <internal/evsel.h>

void perf_evlist__init(struct perf_evlist *evlist)
{
	INIT_LIST_HEAD(&evlist->entries);
}

void perf_evlist__add(struct perf_evlist *evlist,
		      struct perf_evsel *evsel)
{
	list_add_tail(&evsel->node, &evlist->entries);
}

void perf_evlist__remove(struct perf_evlist *evlist,
			 struct perf_evsel *evsel)
{
	list_del_init(&evsel->node);
}
