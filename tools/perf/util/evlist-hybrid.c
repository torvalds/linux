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
