// SPDX-License-Identifier: GPL-2.0
#include <perf/evlist.h>
#include <linux/list.h>
#include <internal/evlist.h>
#include <internal/evsel.h>
#include <linux/zalloc.h>
#include <stdlib.h>

void perf_evlist__init(struct perf_evlist *evlist)
{
	INIT_LIST_HEAD(&evlist->entries);
	evlist->nr_entries = 0;
}

void perf_evlist__add(struct perf_evlist *evlist,
		      struct perf_evsel *evsel)
{
	list_add_tail(&evsel->node, &evlist->entries);
	evlist->nr_entries += 1;
}

void perf_evlist__remove(struct perf_evlist *evlist,
			 struct perf_evsel *evsel)
{
	list_del_init(&evsel->node);
	evlist->nr_entries -= 1;
}

struct perf_evlist *perf_evlist__new(void)
{
	struct perf_evlist *evlist = zalloc(sizeof(*evlist));

	if (evlist != NULL)
		perf_evlist__init(evlist);

	return evlist;
}

struct perf_evsel *
perf_evlist__next(struct perf_evlist *evlist, struct perf_evsel *prev)
{
	struct perf_evsel *next;

	if (!prev) {
		next = list_first_entry(&evlist->entries,
					struct perf_evsel,
					node);
	} else {
		next = list_next_entry(prev, node);
	}

	/* Empty list is noticed here so don't need checking on entry. */
	if (&next->node == &evlist->entries)
		return NULL;

	return next;
}

void perf_evlist__delete(struct perf_evlist *evlist)
{
	free(evlist);
}
