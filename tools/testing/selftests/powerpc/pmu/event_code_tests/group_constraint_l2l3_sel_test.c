// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright 2022, Kajol Jain, IBM Corp.
 */

#include <stdio.h>
#include <stdlib.h>

#include "../event.h"
#include "utils.h"
#include "../sampling_tests/misc.h"

/* All successful D-side store dispatches for this thread */
#define EventCode_1 0x010000046080
/* All successful D-side store dispatches for this thread that were L2 Miss */
#define EventCode_2 0x26880
/* All successful D-side store dispatches for this thread that were L2 Miss */
#define EventCode_3 0x010000026880

/*
 * Testcase for group constraint check of l2l3_sel bits which is
 * used to program l2l3 select field in Monitor Mode Control Register 0
 * (MMCR0: 56-60).
 * All events in the group should match l2l3_sel bits otherwise
 * event_open for the group should fail.
 */
static int group_constraint_l2l3_sel(void)
{
	struct event event, leader;

	/*
	 * Check for platform support for the test.
	 * This test is only aplicable on power10
	 */
	SKIP_IF(platform_check_for_tests());
	SKIP_IF(!have_hwcap2(PPC_FEATURE2_ARCH_3_1));

	/* Init the events for the group contraint check for l2l3_sel bits */
	event_init(&leader, EventCode_1);
	FAIL_IF(event_open(&leader));

	event_init(&event, EventCode_2);

	/* Expected to fail as sibling event doesn't request same l2l3_sel bits as leader */
	FAIL_IF(!event_open_with_group(&event, leader.fd));

	event_close(&event);

	/* Init the event for the group contraint l2l3_sel test */
	event_init(&event, EventCode_3);

	/* Expected to succeed as sibling event request same l2l3_sel bits as leader */
	FAIL_IF(event_open_with_group(&event, leader.fd));

	event_close(&leader);
	event_close(&event);

	return 0;
}

int main(void)
{
	return test_harness(group_constraint_l2l3_sel, "group_constraint_l2l3_sel");
}
