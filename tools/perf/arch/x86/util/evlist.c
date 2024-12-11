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
	 *
	 * If slots event and topdown metrics events are not in same group, the
	 * topdown metrics events must be first event after the slots event group,
	 * otherwise topdown metrics events can't be regrouped correctly, e.g.
	 *
	 * a. perf stat -e "{instructions,slots},cycles,topdown-retiring" -C0 sleep 1
	 *    WARNING: events were regrouped to match PMUs
	 *     Performance counter stats for 'CPU(s) 0':
	 *         17,923,134      slots
	 *          2,154,855      instructions
	 *          3,015,058      cycles
	 *    <not supported>      topdown-retiring
	 *
	 * If slots event and topdown metrics events are in two groups, the group which
	 * has topdown metrics events must contain only the topdown metrics event,
	 * otherwise topdown metrics event can't be regrouped correctly as well, e.g.
	 *
	 * a. perf stat -e "{instructions,slots},{topdown-retiring,cycles}" -C0 sleep 1
	 *    WARNING: events were regrouped to match PMUs
	 *    Error:
	 *    The sys_perf_event_open() syscall returned with 22 (Invalid argument) for
	 *    event (topdown-retiring)
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
		if (arch_is_topdown_metrics(lhs) && !arch_is_topdown_metrics(rhs) &&
		    lhs->core.leader != rhs->core.leader)
			return -1;
		if (!arch_is_topdown_metrics(lhs) && arch_is_topdown_metrics(rhs) &&
		    lhs->core.leader != rhs->core.leader)
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
