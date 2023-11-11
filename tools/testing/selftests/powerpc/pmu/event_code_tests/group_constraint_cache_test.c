// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright 2022, Kajol Jain, IBM Corp.
 */

#include <stdio.h>
#include <stdlib.h>

#include "../event.h"
#include "utils.h"
#include "../sampling_tests/misc.h"

/* All L1 D cache load references counted at finish, gated by reject */
#define EventCode_1 0x1100fc
/* Load Missed L1 */
#define EventCode_2 0x23e054
/* Load Missed L1 */
#define EventCode_3 0x13e054

/*
 * Testcase for group constraint check of data and instructions
 * cache qualifier bits which is used to program cache select field in
 * Monitor Mode Control Register 1 (MMCR1: 16-17) for l1 cache.
 * All events in the group should match cache select bits otherwise
 * event_open for the group will fail.
 */
static int group_constraint_cache(void)
{
	struct event event, leader;

	/* Check for platform support for the test */
	SKIP_IF(platform_check_for_tests());

	/* Init the events for the group contraint check for l1 cache select bits */
	event_init(&leader, EventCode_1);
	FAIL_IF(event_open(&leader));

	event_init(&event, EventCode_2);

	/* Expected to fail as sibling event doesn't request same l1 cache select bits as leader */
	FAIL_IF(!event_open_with_group(&event, leader.fd));

	event_close(&event);

	/* Init the event for the group contraint l1 cache select test */
	event_init(&event, EventCode_3);

	/* Expected to succeed as sibling event request same l1 cache select bits as leader */
	FAIL_IF(event_open_with_group(&event, leader.fd));

	event_close(&leader);
	event_close(&event);

	return 0;
}

int main(void)
{
	return test_harness(group_constraint_cache, "group_constraint_cache");
}
