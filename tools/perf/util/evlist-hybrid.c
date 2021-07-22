// SPDX-License-Identifier: GPL-2.0-only
#include <errno.h>
#include <inttypes.h>
#include "cpumap.h"
#include "evlist.h"
#include "evsel.h"
#include "../perf.h"
#include "util/pmu-hybrid.h"
#include "util/evlist-hybrid.h"
#include "debug.h"
#include <unistd.h>
#include <stdlib.h>
#include <linux/err.h>
#include <linux/string.h>
#include <perf/evlist.h>
#include <perf/evsel.h>
#include <perf/cpumap.h>

int evlist__add_default_hybrid(struct evlist *evlist, bool precise)
{
	struct evsel *evsel;
	struct perf_pmu *pmu;
	__u64 config;
	struct perf_cpu_map *cpus;

	perf_pmu__for_each_hybrid_pmu(pmu) {
		config = PERF_COUNT_HW_CPU_CYCLES |
			 ((__u64)pmu->type << PERF_PMU_TYPE_SHIFT);
		evsel = evsel__new_cycles(precise, PERF_TYPE_HARDWARE,
					  config);
		if (!evsel)
			return -ENOMEM;

		cpus = perf_cpu_map__get(pmu->cpus);
		evsel->core.cpus = cpus;
		evsel->core.own_cpus = perf_cpu_map__get(cpus);
		evsel->pmu_name = strdup(pmu->name);
		evlist__add(evlist, evsel);
	}

	return 0;
}

static bool group_hybrid_conflict(struct evsel *leader)
{
	struct evsel *pos, *prev = NULL;

	for_each_group_evsel(pos, leader) {
		if (!evsel__is_hybrid(pos))
			continue;

		if (prev && strcmp(prev->pmu_name, pos->pmu_name))
			return true;

		prev = pos;
	}

	return false;
}

void evlist__warn_hybrid_group(struct evlist *evlist)
{
	struct evsel *evsel;

	evlist__for_each_entry(evlist, evsel) {
		if (evsel__is_group_leader(evsel) &&
		    evsel->core.nr_members > 1 &&
		    group_hybrid_conflict(evsel)) {
			pr_warning("WARNING: events in group from "
				   "different hybrid PMUs!\n");
			return;
		}
	}
}

bool evlist__has_hybrid(struct evlist *evlist)
{
	struct evsel *evsel;

	evlist__for_each_entry(evlist, evsel) {
		if (evsel->pmu_name &&
		    perf_pmu__is_hybrid(evsel->pmu_name)) {
			return true;
		}
	}

	return false;
}
