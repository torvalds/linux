// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright 2022, Athira Rajeev, IBM Corp.
 */

#include <stdio.h>
#include "../event.h"
#include <sys/prctl.h>
#include <limits.h>
#include "../sampling_tests/misc.h"

/*
 * Testcase for group constraint check for
 * Performance Monitor Counter 5 (PMC5) and also
 * Performance Monitor Counter 6 (PMC6).
 * Test that pmc5/6 is excluded from constraint
 * check when scheduled along with group of events.
 */

static int group_pmc56_exclude_constraints(void)
{
	struct event *e, events[3];
	int i;

	/* Check for platform support for the test */
	SKIP_IF(platform_check_for_tests());

	/*
	 * PMC5/6 is excluded from constraint bit
	 * check along with group of events. Use
	 * group of events with PMC5, PMC6 and also
	 * event with cache bit (dc_ic) set. Test expects
	 * this set of events to go in as a group.
	 */
	e = &events[0];
	event_init(e, 0x500fa);

	e = &events[1];
	event_init(e, 0x600f4);

	e = &events[2];
	event_init(e, 0x22C040);

	FAIL_IF(event_open(&events[0]));

	/*
	 * The event_open will fail if constraint check fails.
	 * Since we are asking for events in a group and since
	 * PMC5/PMC6 is excluded from group constraints, even_open
	 * should pass.
	 */
	for (i = 1; i < 3; i++)
		FAIL_IF(event_open_with_group(&events[i], events[0].fd));

	for (i = 0; i < 3; i++)
		event_close(&events[i]);

	return 0;
}

int main(void)
{
	return test_harness(group_pmc56_exclude_constraints, "group_pmc56_exclude_constraints");
}
