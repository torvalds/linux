// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright 2022, Kajol Jain, IBM Corp.
 */

#include <stdio.h>
#include <stdlib.h>

#include "../event.h"
#include "utils.h"
#include "../sampling_tests/misc.h"

/*
 * Primary PMU events used here is PM_MRK_INST_CMPL (0x401e0) and
 * PM_THRESH_MET (0x101ec)
 * Threshold event selection used is issue to complete for cycles
 * Sampling criteria is Load or Store only sampling
 */
#define p9_EventCode_1 0x13e35340401e0
#define p9_EventCode_2 0x17d34340101ec
#define p9_EventCode_3 0x13e35340101ec
#define p10_EventCode_1 0x35340401e0
#define p10_EventCode_2 0x35340101ec

/*
 * Testcase for group constraint check of thresh_cmp bits which is
 * used to program thresh compare field in Monitor Mode Control Register A
 * (MMCRA: 9-18 bits for power9 and MMCRA: 8-18 bits for power10/power11).
 * All events in the group should match thresh compare bits otherwise
 * event_open for the group will fail.
 */
static int group_constraint_thresh_cmp(void)
{
	struct event event, leader;

	/* Check for platform support for the test */
	SKIP_IF(platform_check_for_tests());

	if (have_hwcap2(PPC_FEATURE2_ARCH_3_1)) {
		/* Init the events for the group contraint check for thresh_cmp bits */
		event_init(&leader, p10_EventCode_1);

		/* Add the thresh_cmp value for leader in config1 */
		leader.attr.config1 = 1000;
		FAIL_IF(event_open(&leader));

		event_init(&event, p10_EventCode_2);

		/* Add the different thresh_cmp value from the leader event in config1 */
		event.attr.config1 = 2000;

		/* Expected to fail as sibling and leader event request different thresh_cmp bits */
		FAIL_IF(!event_open_with_group(&event, leader.fd));

		event_close(&event);

		/* Init the event for the group contraint thresh compare test */
		event_init(&event, p10_EventCode_2);

		/* Add the same thresh_cmp value for leader and sibling event in config1 */
		event.attr.config1 = 1000;

		/* Expected to succeed as sibling and leader event request same thresh_cmp bits */
		FAIL_IF(event_open_with_group(&event, leader.fd));

		event_close(&leader);
		event_close(&event);
	} else {
		/* Init the events for the group contraint check for thresh_cmp bits */
		event_init(&leader, p9_EventCode_1);
		FAIL_IF(event_open(&leader));

		event_init(&event, p9_EventCode_2);

		/* Expected to fail as sibling and leader event request different thresh_cmp bits */
		FAIL_IF(!event_open_with_group(&event, leader.fd));

		event_close(&event);

		/* Init the event for the group contraint thresh compare test */
		event_init(&event, p9_EventCode_3);

		/* Expected to succeed as sibling and leader event request same thresh_cmp bits */
		FAIL_IF(event_open_with_group(&event, leader.fd));

		event_close(&leader);
		event_close(&event);
	}

	return 0;
}

int main(void)
{
	return test_harness(group_constraint_thresh_cmp, "group_constraint_thresh_cmp");
}
