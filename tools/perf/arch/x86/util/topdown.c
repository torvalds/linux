// SPDX-License-Identifier: GPL-2.0
#include <stdio.h>
#include "api/fs/fs.h"
#include "util/pmu.h"
#include "util/topdown.h"

/*
 * Check whether we can use a group for top down.
 * Without a group may get bad results due to multiplexing.
 */
bool arch_topdown_check_group(bool *warn)
{
	int n;

	if (sysctl__read_int("kernel/nmi_watchdog", &n) < 0)
		return false;
	if (n > 0) {
		*warn = true;
		return false;
	}
	return true;
}

void arch_topdown_group_warn(void)
{
	fprintf(stderr,
		"nmi_watchdog enabled with topdown. May give wrong results.\n"
		"Disable with echo 0 > /proc/sys/kernel/nmi_watchdog\n");
}

#define TOPDOWN_SLOTS		0x0400

static bool is_topdown_slots_event(struct evsel *counter)
{
	if (!counter->pmu_name)
		return false;

	if (strcmp(counter->pmu_name, "cpu"))
		return false;

	if (counter->core.attr.config == TOPDOWN_SLOTS)
		return true;

	return false;
}

/*
 * Check whether a topdown group supports sample-read.
 *
 * Only Topdown metic supports sample-read. The slots
 * event must be the leader of the topdown group.
 */

bool arch_topdown_sample_read(struct evsel *leader)
{
	if (!pmu_have_event("cpu", "slots"))
		return false;

	if (is_topdown_slots_event(leader))
		return true;

	return false;
}
