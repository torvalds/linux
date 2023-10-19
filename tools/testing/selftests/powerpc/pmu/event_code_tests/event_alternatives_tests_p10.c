// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright 2022, Athira Rajeev, IBM Corp.
 */

#include <stdio.h>
#include "../event.h"
#include "../sampling_tests/misc.h"

#define PM_RUN_CYC_ALT 0x200f4
#define PM_INST_DISP 0x200f2
#define PM_BR_2PATH 0x20036
#define PM_LD_MISS_L1 0x3e054
#define PM_RUN_INST_CMPL_ALT 0x400fa

#define EventCode_1 0x100fc
#define EventCode_2 0x200fa
#define EventCode_3 0x300fc
#define EventCode_4 0x400fc

/*
 * Check for event alternatives.
 */

static int event_alternatives_tests_p10(void)
{
	struct event *e, events[5];
	int i;

	/* Check for platform support for the test */
	SKIP_IF(platform_check_for_tests());

	/*
	 * PVR check is used here since PMU specific data like
	 * alternative events is handled by respective PMU driver
	 * code and using PVR will work correctly for all cases
	 * including generic compat mode.
	 */
	SKIP_IF(PVR_VER(mfspr(SPRN_PVR)) != POWER10);

	SKIP_IF(check_for_generic_compat_pmu());

	/*
	 * Test for event alternative for 0x0001e
	 * and 0x00002.
	 */
	e = &events[0];
	event_init(e, 0x0001e);

	e = &events[1];
	event_init(e, EventCode_1);

	e = &events[2];
	event_init(e, EventCode_2);

	e = &events[3];
	event_init(e, EventCode_3);

	e = &events[4];
	event_init(e, EventCode_4);

	FAIL_IF(event_open(&events[0]));

	/*
	 * Expected to pass since 0x0001e has alternative event
	 * 0x600f4 in PMC6. So it can go in with other events
	 * in PMC1 to PMC4.
	 */
	for (i = 1; i < 5; i++)
		FAIL_IF(event_open_with_group(&events[i], events[0].fd));

	for (i = 0; i < 5; i++)
		event_close(&events[i]);

	e = &events[0];
	event_init(e, 0x00002);

	e = &events[1];
	event_init(e, EventCode_1);

	e = &events[2];
	event_init(e, EventCode_2);

	e = &events[3];
	event_init(e, EventCode_3);

	e = &events[4];
	event_init(e, EventCode_4);

	FAIL_IF(event_open(&events[0]));

	/*
	 * Expected to pass since 0x00020 has alternative event
	 * 0x500fa in PMC5. So it can go in with other events
	 * in PMC1 to PMC4.
	 */
	for (i = 1; i < 5; i++)
		FAIL_IF(event_open_with_group(&events[i], events[0].fd));

	for (i = 0; i < 5; i++)
		event_close(&events[i]);

	return 0;
}

int main(void)
{
	return test_harness(event_alternatives_tests_p10, "event_alternatives_tests_p10");
}
