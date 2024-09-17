// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright 2022, Athira Rajeev, IBM Corp.
 */

#include <stdio.h>
#include "../event.h"
#include "../sampling_tests/misc.h"

/*
 * Testcase for number of counters in use.
 * The number of programmable counters is from
 * performance monitor counter 1 to performance
 * monitor counter 4 (PMC1-PMC4). If number of
 * counters in use exceeds the limit, next event
 * should fail to schedule.
 */

static int group_constraint_pmc_count(void)
{
	struct event *e, events[5];
	int i;

	/* Check for platform support for the test */
	SKIP_IF(platform_check_for_tests());

	/*
	 * Test for number of counters in use.
	 * Use PMC1 to PMC4 for leader and 3 sibling
	 * events. Trying to open fourth event should
	 * fail here.
	 */
	e = &events[0];
	event_init(e, 0x1001a);

	e = &events[1];
	event_init(e, 0x200fc);

	e = &events[2];
	event_init(e, 0x30080);

	e = &events[3];
	event_init(e, 0x40054);

	e = &events[4];
	event_init(e, 0x0002c);

	FAIL_IF(event_open(&events[0]));

	/*
	 * The event_open will fail on event 4 if constraint
	 * check fails
	 */
	for (i = 1; i < 5; i++) {
		if (i == 4)
			FAIL_IF(!event_open_with_group(&events[i], events[0].fd));
		else
			FAIL_IF(event_open_with_group(&events[i], events[0].fd));
	}

	for (i = 1; i < 4; i++)
		event_close(&events[i]);

	return 0;
}

int main(void)
{
	return test_harness(group_constraint_pmc_count, "group_constraint_pmc_count");
}
