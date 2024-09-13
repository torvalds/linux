// SPDX-License-Identifier: GPL-2.0
#include <string.h>
#include "../../../util/evlist.h"
#include "../../../util/evsel.h"
#include "topdown.h"
#include "evsel.h"

int arch_evlist__cmp(const struct evsel *lhs, const struct evsel *rhs)
{
	if (topdown_sys_has_perf_metrics() &&
	    (arch_evsel__must_be_in_group(lhs) || arch_evsel__must_be_in_group(rhs))) {
		/* Ensure the topdown slots comes first. */
		if (arch_is_topdown_slots(lhs))
			return -1;
		if (arch_is_topdown_slots(rhs))
			return 1;
		/* Followed by topdown events. */
		if (arch_is_topdown_metrics(lhs) && !arch_is_topdown_metrics(rhs))
			return -1;
		if (!arch_is_topdown_metrics(lhs) && arch_is_topdown_metrics(rhs))
			return 1;
	}

	/* Retire latency event should not be group leader*/
	if (lhs->retire_lat && !rhs->retire_lat)
		return 1;
	if (!lhs->retire_lat && rhs->retire_lat)
		return -1;

	/* Default ordering by insertion index. */
	return lhs->core.idx - rhs->core.idx;
}
