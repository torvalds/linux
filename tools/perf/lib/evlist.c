// SPDX-License-Identifier: GPL-2.0
#include <perf/evlist.h>
#include <perf/evsel.h>
#include <linux/list.h>
#include <internal/evlist.h>
#include <internal/evsel.h>
#include <linux/zalloc.h>
#include <stdlib.h>
#include <perf/cpumap.h>
#include <perf/threadmap.h>

void perf_evlist__init(struct perf_evlist *evlist)
{
	INIT_LIST_HEAD(&evlist->entries);
	evlist->nr_entries = 0;
}

static void __perf_evlist__propagate_maps(struct perf_evlist *evlist,
					  struct perf_evsel *evsel)
{
	/*
	 * We already have cpus for evsel (via PMU sysfs) so
	 * keep it, if there's no target cpu list defined.
	 */
	if (!evsel->own_cpus || evlist->has_user_cpus) {
		perf_cpu_map__put(evsel->cpus);
		evsel->cpus = perf_cpu_map__get(evlist->cpus);
	} else if (evsel->cpus != evsel->own_cpus) {
		perf_cpu_map__put(evsel->cpus);
		evsel->cpus = perf_cpu_map__get(evsel->own_cpus);
	}

	perf_thread_map__put(evsel->threads);
	evsel->threads = perf_thread_map__get(evlist->threads);
}

static void perf_evlist__propagate_maps(struct perf_evlist *evlist)
{
	struct perf_evsel *evsel;

	perf_evlist__for_each_evsel(evlist, evsel)
		__perf_evlist__propagate_maps(evlist, evsel);
}

void perf_evlist__add(struct perf_evlist *evlist,
		      struct perf_evsel *evsel)
{
	list_add_tail(&evsel->node, &evlist->entries);
	evlist->nr_entries += 1;
	__perf_evlist__propagate_maps(evlist, evsel);
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

void perf_evlist__set_maps(struct perf_evlist *evlist,
			   struct perf_cpu_map *cpus,
			   struct perf_thread_map *threads)
{
	/*
	 * Allow for the possibility that one or another of the maps isn't being
	 * changed i.e. don't put it.  Note we are assuming the maps that are
	 * being applied are brand new and evlist is taking ownership of the
	 * original reference count of 1.  If that is not the case it is up to
	 * the caller to increase the reference count.
	 */
	if (cpus != evlist->cpus) {
		perf_cpu_map__put(evlist->cpus);
		evlist->cpus = perf_cpu_map__get(cpus);
	}

	if (threads != evlist->threads) {
		perf_thread_map__put(evlist->threads);
		evlist->threads = perf_thread_map__get(threads);
	}

	perf_evlist__propagate_maps(evlist);
}

int perf_evlist__open(struct perf_evlist *evlist)
{
	struct perf_evsel *evsel;
	int err;

	perf_evlist__for_each_entry(evlist, evsel) {
		err = perf_evsel__open(evsel, evsel->cpus, evsel->threads);
		if (err < 0)
			goto out_err;
	}

	return 0;

out_err:
	perf_evlist__close(evlist);
	return err;
}

void perf_evlist__close(struct perf_evlist *evlist)
{
	struct perf_evsel *evsel;

	perf_evlist__for_each_entry_reverse(evlist, evsel)
		perf_evsel__close(evsel);
}

void perf_evlist__enable(struct perf_evlist *evlist)
{
	struct perf_evsel *evsel;

	perf_evlist__for_each_entry(evlist, evsel)
		perf_evsel__enable(evsel);
}

void perf_evlist__disable(struct perf_evlist *evlist)
{
	struct perf_evsel *evsel;

	perf_evlist__for_each_entry(evlist, evsel)
		perf_evsel__disable(evsel);
}
