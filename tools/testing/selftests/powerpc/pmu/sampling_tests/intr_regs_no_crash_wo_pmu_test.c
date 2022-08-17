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
 * sampling with -intr-regs doesn't crash
 * in any environment, say:
 *  - With generic compat PMU
 *  - without any PMU registered
 *  - With platform specific PMU.
 *  A fix for crash with intr_regs was
 *  addressed in commit: f75e7d73bdf7 in kernel.
 *
 * This testcase exercises this code path by doing
 * intr_regs using software event. Software event is
 * used since s/w event will work even in platform
 * without PMU.
 */
static int intr_regs_no_crash_wo_pmu_test(void)
{
	struct event event;

	/*
	 * Init the event for the sampling test.
	 * This uses software event which works on
	 * any platform.
	 */
	event_init_opts(&event, 0, PERF_TYPE_SOFTWARE, "cycles");

	event.attr.sample_period = 1000;
	event.attr.sample_type = PERF_SAMPLE_REGS_INTR;
	event.attr.disabled = 1;

	/*
	 * Return code of event_open is not considered
	 * since test just expects no crash from using
	 * PERF_SAMPLE_REGS_INTR.
	 */
	event_open(&event);

	event_close(&event);
	return 0;
}

int main(void)
{
	return test_harness(intr_regs_no_crash_wo_pmu_test, "intr_regs_no_crash_wo_pmu_test");
}
