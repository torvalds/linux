// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright 2022, Athira Rajeev, IBM Corp.
 */

#include <stdio.h>
#include <stdlib.h>

#include "../event.h"
#include "misc.h"
#include "utils.h"

/*
 * A perf sampling test for making sure
 * enabling branch stack doesn't crash in any
 * environment, say:
 *  - With generic compat PMU
 *  - without any PMU registered
 *  - With platform specific PMU
 *  A fix for bhrb sampling crash was added in kernel
 *  via commit: b460b512417a ("powerpc/perf: Fix crashes
 *  with generic_compat_pmu & BHRB")
 *
 * This testcase exercises this code by doing branch
 * stack enable for software event. s/w event is used
 * since software event will work even in platform
 * without PMU.
 */
static int bhrb_no_crash_wo_pmu_test(void)
{
	struct event event;

	/*
	 * Init the event for the sampling test.
	 * This uses software event which works on
	 * any platform.
	 */
	event_init_opts(&event, 0, PERF_TYPE_SOFTWARE, "cycles");

	event.attr.sample_period = 1000;
	event.attr.sample_type = PERF_SAMPLE_BRANCH_STACK;
	event.attr.disabled = 1;

	/*
	 * Return code of event_open is not
	 * considered since test just expects no crash from
	 * using PERF_SAMPLE_BRANCH_STACK. Also for environment
	 * like generic compat PMU, branch stack is unsupported.
	 */
	event_open(&event);

	event_close(&event);
	return 0;
}

int main(void)
{
	return test_harness(bhrb_no_crash_wo_pmu_test, "bhrb_no_crash_wo_pmu_test");
}
