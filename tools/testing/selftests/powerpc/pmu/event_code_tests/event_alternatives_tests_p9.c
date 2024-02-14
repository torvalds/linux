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

#define EventCode_1 0x200fa
#define EventCode_2 0x200fc
#define EventCode_3 0x300fc
#define EventCode_4 0x400fc

/*
 * Check for event alternatives.
 */

static int event_alternatives_tests_p9(void)
{
	struct event event, leader;

	/* Check for platform support for the test */
	SKIP_IF(platform_check_for_tests());

	/*
	 * PVR check is used here since PMU specific data like
	 * alternative events is handled by respective PMU driver
	 * code and using PVR will work correctly for all cases
	 * including generic compat mode.
	 */
	SKIP_IF(PVR_VER(mfspr(SPRN_PVR)) != POWER9);

	/* Skip for generic compat PMU */
	SKIP_IF(check_for_generic_compat_pmu());

	/* Init the event for PM_RUN_CYC_ALT */
	event_init(&leader, PM_RUN_CYC_ALT);
	FAIL_IF(event_open(&leader));

	event_init(&event, EventCode_1);

	/*
	 * Expected to pass since PM_RUN_CYC_ALT in PMC2 has alternative event
	 * 0x600f4. So it can go in with EventCode_1 which is using PMC2
	 */
	FAIL_IF(event_open_with_group(&event, leader.fd));

	event_close(&leader);
	event_close(&event);

	event_init(&leader, PM_INST_DISP);
	FAIL_IF(event_open(&leader));

	event_init(&event, EventCode_2);
	/*
	 * Expected to pass since PM_INST_DISP in PMC2 has alternative event
	 * 0x300f2 in PMC3. So it can go in with EventCode_2 which is using PMC2
	 */
	FAIL_IF(event_open_with_group(&event, leader.fd));

	event_close(&leader);
	event_close(&event);

	event_init(&leader, PM_BR_2PATH);
	FAIL_IF(event_open(&leader));

	event_init(&event, EventCode_2);
	/*
	 * Expected to pass since PM_BR_2PATH in PMC2 has alternative event
	 * 0x40036 in PMC4. So it can go in with EventCode_2 which is using PMC2
	 */
	FAIL_IF(event_open_with_group(&event, leader.fd));

	event_close(&leader);
	event_close(&event);

	event_init(&leader, PM_LD_MISS_L1);
	FAIL_IF(event_open(&leader));

	event_init(&event, EventCode_3);
	/*
	 * Expected to pass since PM_LD_MISS_L1 in PMC3 has alternative event
	 * 0x400f0 in PMC4. So it can go in with EventCode_3 which is using PMC3
	 */
	FAIL_IF(event_open_with_group(&event, leader.fd));

	event_close(&leader);
	event_close(&event);

	event_init(&leader, PM_RUN_INST_CMPL_ALT);
	FAIL_IF(event_open(&leader));

	event_init(&event, EventCode_4);
	/*
	 * Expected to pass since PM_RUN_INST_CMPL_ALT in PMC4 has alternative event
	 * 0x500fa in PMC5. So it can go in with EventCode_4 which is using PMC4
	 */
	FAIL_IF(event_open_with_group(&event, leader.fd));

	event_close(&leader);
	event_close(&event);

	return 0;
}

int main(void)
{
	return test_harness(event_alternatives_tests_p9, "event_alternatives_tests_p9");
}
