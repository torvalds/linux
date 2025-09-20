// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright 2022, Athira Rajeev, IBM Corp.
 */

#include <stdio.h>
#include "../event.h"
#include "../sampling_tests/misc.h"

#define EventCode_1 0x35340401e0
#define EventCode_2 0x353c0101ec
#define EventCode_3 0x35340101ec
/*
 * Test that using different sample bits in
 * event code cause failure in schedule for
 * group of events.
 */

static int group_constraint_mmcra_sample(void)
{
	struct event event, leader;

	SKIP_IF(platform_check_for_tests());

	/*
	 * Events with different "sample" field values
	 * in a group will fail to schedule.
	 * Use event with load only sampling mode as
	 * group leader. Use event with store only sampling
	 * as sibling event.
	 */
	event_init(&leader, EventCode_1);
	FAIL_IF(event_open(&leader));

	event_init(&event, EventCode_2);

	/* Expected to fail as sibling event doesn't use same sampling bits as leader */
	FAIL_IF(!event_open_with_group(&event, leader.fd));

	event_init(&event, EventCode_3);

	/* Expected to pass as sibling event use same sampling bits as leader */
	FAIL_IF(event_open_with_group(&event, leader.fd));

	event_close(&leader);
	event_close(&event);

	return 0;
}

int main(void)
{
	return test_harness(group_constraint_mmcra_sample, "group_constraint_mmcra_sample");
}
