// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright 2022, Athira Rajeev, IBM Corp.
 */

#include <stdio.h>
#include "../event.h"
#include "../sampling_tests/misc.h"

/*
 * Testcase for checking constraint checks for
 * Performance Monitor Counter 5 (PMC5) and also
 * Performance Monitor Counter 6 (PMC6). Events using
 * PMC5/PMC6 shouldn't have other fields in event
 * code like cache bits, thresholding or marked bit.
 */

static int group_constraint_pmc56(void)
{
	struct event event;

	/* Check for platform support for the test */
	SKIP_IF(platform_check_for_tests());

	/*
	 * Events using PMC5 and PMC6 with cache bit
	 * set in event code is expected to fail.
	 */
	event_init(&event, 0x2500fa);
	FAIL_IF(!event_open(&event));

	event_init(&event, 0x2600f4);
	FAIL_IF(!event_open(&event));

	/*
	 * PMC5 and PMC6 only supports base events:
	 * ie 500fa and 600f4. Other combinations
	 * should fail.
	 */
	event_init(&event, 0x501e0);
	FAIL_IF(!event_open(&event));

	event_init(&event, 0x6001e);
	FAIL_IF(!event_open(&event));

	event_init(&event, 0x501fa);
	FAIL_IF(!event_open(&event));

	/*
	 * Events using PMC5 and PMC6 with random
	 * sampling bits set in event code should fail
	 * to schedule.
	 */
	event_init(&event, 0x35340500fa);
	FAIL_IF(!event_open(&event));

	return 0;
}

int main(void)
{
	return test_harness(group_constraint_pmc56, "group_constraint_pmc56");
}
