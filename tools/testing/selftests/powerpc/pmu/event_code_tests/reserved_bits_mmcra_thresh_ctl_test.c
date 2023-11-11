// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright 2022, Athira Rajeev, IBM Corp.
 */

#include <stdio.h>
#include "../event.h"
#include "../sampling_tests/misc.h"

/*
 * Testcase for reserved bits in Monitor Mode
 * Control Register A (MMCRA) thresh_ctl bits.
 * For MMCRA[48:51]/[52:55]) Threshold Start/Stop,
 * 0b11110000/0b00001111 is reserved.
 */

static int reserved_bits_mmcra_thresh_ctl(void)
{
	struct event event;

	/* Check for platform support for the test */
	SKIP_IF(platform_check_for_tests());

	/* Skip for Generic compat PMU */
	SKIP_IF(check_for_generic_compat_pmu());

	/*
	 * MMCRA[48:51]/[52:55]) Threshold Start/Stop
	 * events Selection. 0b11110000/0b00001111 is reserved.
	 * Expected to fail when using these reserved values.
	 */
	event_init(&event, 0xf0340401e0);
	FAIL_IF(!event_open(&event));

	event_init(&event, 0x0f340401e0);
	FAIL_IF(!event_open(&event));

	return 0;
}

int main(void)
{
	return test_harness(reserved_bits_mmcra_thresh_ctl, "reserved_bits_mmcra_thresh_ctl");
}
