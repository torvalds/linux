// SPDX-License-Identifier: GPL-2.0-only
#include <errno.h>
#include <inttypes.h>
#include "cpumap.h"
#include "evlist.h"
#include "evsel.h"
#include "../perf.h"
#include "util/pmu-hybrid.h"
#include "util/evlist-hybrid.h"
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
