// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright 2022, Athira Rajeev, IBM Corp.
 */

#include <stdio.h>
#include "../event.h"
#include "../sampling_tests/misc.h"

/* PM_DATA_RADIX_PROCESS_L2_PTE_FROM_L2 */
#define EventCode_1 0x14242
/* PM_DATA_RADIX_PROCESS_L2_PTE_FROM_L3 */
#define EventCode_2 0x24242

/*
 * Testcase for group constraint check for radix_scope_qual
 * field which is used to program Monitor Mode Control
 * egister (MMCR1)  bit 18.
 * All events in the group should match radix_scope_qual,
 * bits otherwise event_open for the group should fail.
 */

static int group_constraint_radix_scope_qual(void)
{
	struct event event, leader;

	/*
	 * Check for platform support for the test.
	 * This test is aplicable on ISA v3.1 only.
	 */
	SKIP_IF(platform_check_for_tests());
	SKIP_IF(!have_hwcap2(PPC_FEATURE2_ARCH_3_1));

	/* Init the events for the group contraint check for radix_scope_qual bits */
	event_init(&leader, EventCode_1);
	FAIL_IF(event_open(&leader));

	event_init(&event, 0x200fc);

	/* Expected to fail as sibling event doesn't request same radix_scope_qual bits as leader */
	FAIL_IF(!event_open_with_group(&event, leader.fd));

	event_init(&event, EventCode_2);
	/* Expected to pass as sibling event request same radix_scope_qual bits as leader */
	FAIL_IF(event_open_with_group(&event, leader.fd));

	event_close(&leader);
	event_close(&event);
	return 0;
}

int main(void)
{
	return test_harness(group_constraint_radix_scope_qual,
			    "group_constraint_radix_scope_qual");
}
