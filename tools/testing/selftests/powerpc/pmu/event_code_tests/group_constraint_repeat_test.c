// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright 2022, Athira Rajeev, IBM Corp.
 */

#include <stdio.h>
#include "../event.h"
#include "../sampling_tests/misc.h"

/* The processor's L1 data cache was reloaded */
#define EventCode1 0x21C040
#define EventCode2 0x22C040

/*
 * Testcase for group constraint check
 * when using events with same PMC.
 * Multiple events in a group shouldn't
 * ask for same PMC. If so it should fail.
 */

static int group_constraint_repeat(void)
{
	struct event event, leader;

	/* Check for platform support for the test */
	SKIP_IF(platform_check_for_tests());

	/*
	 * Two events in a group using same PMC
	 * should fail to get scheduled. Usei same PMC2
	 * for leader and sibling event which is expected
	 * to fail.
	 */
	event_init(&leader, EventCode1);
	FAIL_IF(event_open(&leader));

	event_init(&event, EventCode1);

	/* Expected to fail since sibling event is requesting same PMC as leader */
	FAIL_IF(!event_open_with_group(&event, leader.fd));

	event_init(&event, EventCode2);

	/* Expected to pass since sibling event is requesting different PMC */
	FAIL_IF(event_open_with_group(&event, leader.fd));

	event_close(&leader);
	event_close(&event);

	return 0;
}

int main(void)
{
	return test_harness(group_constraint_repeat, "group_constraint_repeat");
}
