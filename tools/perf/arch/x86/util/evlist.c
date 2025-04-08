// SPDX-License-Identifier: GPL-2.0
#include <string.h>
#include "../../../util/evlist.h"
#include "../../../util/evsel.h"
#include "topdown.h"
#include "evsel.h"

int arch_evlist__cmp(const struct evsel *lhs, const struct evsel *rhs)
{
	/*
	 * Currently the following topdown events sequence are supported to
	 * move and regroup correctly.
	 *
	 * a. all events in a group
	 *    perf stat -e "{instructions,topdown-retiring,slots}" -C0 sleep 1
	 *    WARNING: events were regrouped to match PMUs
	 *     Performance counter stats for 'CPU(s) 0':
	 *          15,066,240     slots
	 *          1,899,760      instructions
	 *          2,126,998      topdown-retiring
	 * b. all events not in a group
	 *    perf stat -e "instructions,topdown-retiring,slots" -C0 sleep 1
	 *    WARNING: events were regrouped to match PMUs
	 *     Performance counter stats for 'CPU(s) 0':
	 *          2,045,561      instructions
	 *          17,108,370     slots
	 *          2,281,116      topdown-retiring
	 * c. slots event in a group but topdown metrics events outside the group
	 *    perf stat -e "{instructions,slots},topdown-retiring" -C0 sleep 1
	 *    WARNING: events were regrouped to match PMUs
	 *     Performance counter stats for 'CPU(s) 0':
	 *         20,323,878      slots
	 *          2,634,884      instructions
	 *          3,028,656      topdown-retiring
	 * d. slots event and topdown metrics events in two groups
	 *    perf stat -e "{instructions,slots},{topdown-retiring}" -C0 sleep 1
	 *    WARNING: events were regrouped to match PMUs
	 *     Performance counter stats for 'CPU(s) 0':
	 *         26,319,024      slots
	 *          2,427,791      instructions
	 *          2,683,508      topdown-retiring
	 * e. slots event and metrics event are not in a group and not adjacent
	 *    perf stat -e "{instructions,slots},cycles,topdown-retiring" -C0 sleep 1
	 *    WARNING: events were regrouped to match PMUs
	 *         68,433,522      slots
	 *          8,856,102      topdown-retiring
	 *          7,791,494      instructions
	 *         11,469,513      cycles
	 */
	if (topdown_sys_has_perf_metrics() &&
	    (arch_evsel__must_be_in_group(lhs) || arch_evsel__must_be_in_group(rhs))) {
		/* Ensure the topdown slots comes first. */
		if (arch_is_topdown_slots(lhs))
			return -1;
		if (arch_is_topdown_slots(rhs))
			return 1;

		/*
		 * Move topdown metrics events forward only when topdown metrics
		 * events are not in same group with previous slots event. If
		 * topdown metrics events are already in same group with slots
		 * event, do nothing.
		 */
		if (lhs->core.leader != rhs->core.leader) {
			bool lhs_topdown = arch_is_topdown_metrics(lhs);
			bool rhs_topdown = arch_is_topdown_metrics(rhs);

			if (lhs_topdown && !rhs_topdown)
				return -1;
			if (!lhs_topdown && rhs_topdown)
				return 1;
		}
	}

	/* Retire latency event should not be group leader*/
	if (lhs->retire_lat && !rhs->retire_lat)
		return 1;
	if (!lhs->retire_lat && rhs->retire_lat)
		return -1;

	/* Default ordering by insertion index. */
	return lhs->core.idx - rhs->core.idx;
}
