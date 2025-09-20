// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright 2022, Kajol Jain, IBM Corp.
 */

#include <stdio.h>
#include <stdlib.h>

#include "../event.h"
#include "utils.h"
#include "../sampling_tests/misc.h"

/* All successful D-side store dispatches for this thread with PMC 2 */
#define EventCode_1 0x26080
/* All successful D-side store dispatches for this thread with PMC 4 */
#define EventCode_2 0x46080
/* All successful D-side store dispatches for this thread that were L2 Miss with PMC 3 */
#define EventCode_3 0x36880

/*
 * Testcase for group constraint check of unit and pmc bits which is
 * used to program corresponding unit and pmc field in Monitor Mode
 * Control Register 1 (MMCR1)
 * One of the event in the group should use PMC 4 incase units field
 * value is within 6 to 9 otherwise event_open for the group will fail.
 */
static int group_constraint_unit(void)
{
	struct event *e, events[3];

	/*
	 * Check for platform support for the test.
	 * Constraint to use PMC4 with one of the event in group,
	 * when the unit is within 6 to 9 is only applicable on
	 * power9.
	 */
	SKIP_IF(platform_check_for_tests());
	SKIP_IF(have_hwcap2(PPC_FEATURE2_ARCH_3_1));

	/* Init the events for the group contraint check for unit bits */
	e = &events[0];
	event_init(e, EventCode_1);

	 /* Expected to fail as PMC 4 is not used with unit field value 6 to 9 */
	FAIL_IF(!event_open(&events[0]));

	/* Init the events for the group contraint check for unit bits */
	e = &events[1];
	event_init(e, EventCode_2);

	/* Expected to pass as PMC 4 is used with unit field value 6 to 9 */
	FAIL_IF(event_open(&events[1]));

	/* Init the event for the group contraint unit test */
	e = &events[2];
	event_init(e, EventCode_3);

	/* Expected to fail as PMC4 is not being used */
	FAIL_IF(!event_open_with_group(&events[2], events[0].fd));

	/* Expected to succeed as event using PMC4 */
	FAIL_IF(event_open_with_group(&events[2], events[1].fd));

	event_close(&events[0]);
	event_close(&events[1]);
	event_close(&events[2]);

	return 0;
}

int main(void)
{
	return test_harness(group_constraint_unit, "group_constraint_unit");
}
