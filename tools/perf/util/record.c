#include "evlist.h"
#include "evsel.h"
#include "cpumap.h"

void perf_evlist__config(struct perf_evlist *evlist,
			struct perf_record_opts *opts)
{
	struct perf_evsel *evsel;
	/*
	 * Set the evsel leader links before we configure attributes,
	 * since some might depend on this info.
	 */
	if (opts->group)
		perf_evlist__set_leader(evlist);

	if (evlist->cpus->map[0] < 0)
		opts->no_inherit = true;

	list_for_each_entry(evsel, &evlist->entries, node) {
		perf_evsel__config(evsel, opts);

		if (evlist->nr_entries > 1)
			perf_evsel__set_sample_id(evsel);
	}
}
