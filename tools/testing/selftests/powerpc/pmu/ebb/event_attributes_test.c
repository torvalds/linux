// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright 2014, Michael Ellerman, IBM Corp.
 */

#include <stdio.h>
#include <stdlib.h>

#include "ebb.h"


/*
 * Test various attributes of the EBB event are enforced.
 */
int event_attributes(void)
{
	struct event event, leader;

	SKIP_IF(!ebb_is_supported());

	event_init(&event, 0x1001e);
	event_leader_ebb_init(&event);
	/* Expected to succeed */
	FAIL_IF(event_open(&event));
	event_close(&event);


	event_init(&event, 0x001e); /* CYCLES - no PMC specified */
	event_leader_ebb_init(&event);
	/* Expected to fail, no PMC specified */
	FAIL_IF(event_open(&event) == 0);


	event_init(&event, 0x2001e);
	event_leader_ebb_init(&event);
	event.attr.exclusive = 0;
	/* Expected to fail, not exclusive */
	FAIL_IF(event_open(&event) == 0);


	event_init(&event, 0x3001e);
	event_leader_ebb_init(&event);
	event.attr.freq = 1;
	/* Expected to fail, sets freq */
	FAIL_IF(event_open(&event) == 0);


	event_init(&event, 0x4001e);
	event_leader_ebb_init(&event);
	event.attr.sample_period = 1;
	/* Expected to fail, sets sample_period */
	FAIL_IF(event_open(&event) == 0);


	event_init(&event, 0x1001e);
	event_leader_ebb_init(&event);
	event.attr.enable_on_exec = 1;
	/* Expected to fail, sets enable_on_exec */
	FAIL_IF(event_open(&event) == 0);


	event_init(&event, 0x1001e);
	event_leader_ebb_init(&event);
	event.attr.inherit = 1;
	/* Expected to fail, sets inherit */
	FAIL_IF(event_open(&event) == 0);


	event_init(&leader, 0x1001e);
	event_leader_ebb_init(&leader);
	FAIL_IF(event_open(&leader));

	event_init(&event, 0x20002);
	event_ebb_init(&event);

	/* Expected to succeed */
	FAIL_IF(event_open_with_group(&event, leader.fd));
	event_close(&leader);
	event_close(&event);


	event_init(&leader, 0x1001e);
	event_leader_ebb_init(&leader);
	FAIL_IF(event_open(&leader));

	event_init(&event, 0x20002);

	/* Expected to fail, event doesn't request EBB, leader does */
	FAIL_IF(event_open_with_group(&event, leader.fd) == 0);
	event_close(&leader);


	event_init(&leader, 0x1001e);
	event_leader_ebb_init(&leader);
	/* Clear the EBB flag */
	leader.attr.config &= ~(1ull << 63);

	FAIL_IF(event_open(&leader));

	event_init(&event, 0x20002);
	event_ebb_init(&event);

	/* Expected to fail, leader doesn't request EBB */
	FAIL_IF(event_open_with_group(&event, leader.fd) == 0);
	event_close(&leader);


	event_init(&leader, 0x1001e);
	event_leader_ebb_init(&leader);
	leader.attr.exclusive = 0;
	/* Expected to fail, leader isn't exclusive */
	FAIL_IF(event_open(&leader) == 0);


	event_init(&leader, 0x1001e);
	event_leader_ebb_init(&leader);
	leader.attr.pinned = 0;
	/* Expected to fail, leader isn't pinned */
	FAIL_IF(event_open(&leader) == 0);

	event_init(&event, 0x1001e);
	event_leader_ebb_init(&event);
	/* Expected to fail, not a task event */
	SKIP_IF(require_paranoia_below(1));
	FAIL_IF(event_open_with_cpu(&event, 0) == 0);

	return 0;
}

int main(void)
{
	return test_harness(event_attributes, "event_attributes");
}
