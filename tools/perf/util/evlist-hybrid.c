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

int evlist__fix_hybrid_cpus(struct evlist *evlist, const char *cpu_list)
{
	struct perf_cpu_map *cpus;
	struct evsel *evsel, *tmp;
	struct perf_pmu *pmu;
	int ret, unmatched_count = 0, events_nr = 0;

	if (!perf_pmu__has_hybrid() || !cpu_list)
		return 0;

	cpus = perf_cpu_map__new(cpu_list);
	if (!cpus)
		return -1;

	/*
	 * The evsels are created with hybrid pmu's cpus. But now we
	 * need to check and adjust the cpus of evsel by cpu_list because
	 * cpu_list may cause conflicts with cpus of evsel. For example,
	 * cpus of evsel is cpu0-7, but the cpu_list is cpu6-8, we need
	 * to adjust the cpus of evsel to cpu6-7. And then propatate maps
	 * in evlist__create_maps().
	 */
	evlist__for_each_entry_safe(evlist, tmp, evsel) {
		struct perf_cpu_map *matched_cpus, *unmatched_cpus;
		char buf1[128], buf2[128];

		pmu = perf_pmu__find_hybrid_pmu(evsel->pmu_name);
		if (!pmu)
			continue;

		ret = perf_pmu__cpus_match(pmu, cpus, &matched_cpus,
					   &unmatched_cpus);
		if (ret)
			goto out;

		events_nr++;

		if (matched_cpus->nr > 0 && (unmatched_cpus->nr > 0 ||
		    matched_cpus->nr < cpus->nr ||
		    matched_cpus->nr < pmu->cpus->nr)) {
			perf_cpu_map__put(evsel->core.cpus);
			perf_cpu_map__put(evsel->core.own_cpus);
			evsel->core.cpus = perf_cpu_map__get(matched_cpus);
			evsel->core.own_cpus = perf_cpu_map__get(matched_cpus);

			if (unmatched_cpus->nr > 0) {
				cpu_map__snprint(matched_cpus, buf1, sizeof(buf1));
				pr_warning("WARNING: use %s in '%s' for '%s', skip other cpus in list.\n",
					   buf1, pmu->name, evsel->name);
			}
		}

		if (matched_cpus->nr == 0) {
			evlist__remove(evlist, evsel);
			evsel__delete(evsel);

			cpu_map__snprint(cpus, buf1, sizeof(buf1));
			cpu_map__snprint(pmu->cpus, buf2, sizeof(buf2));
			pr_warning("WARNING: %s isn't a '%s', please use a CPU list in the '%s' range (%s)\n",
				   buf1, pmu->name, pmu->name, buf2);
			unmatched_count++;
		}

		perf_cpu_map__put(matched_cpus);
		perf_cpu_map__put(unmatched_cpus);
	}
	if (events_nr)
		ret = (unmatched_count == events_nr) ? -1 : 0;
out:
	perf_cpu_map__put(cpus);
	return ret;
}
