// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright 2022, Athira Rajeev, IBM Corp.
 */

#include <stdio.h>
#include <sys/prctl.h>
#include <limits.h>
#include "../event.h"
#include "../sampling_tests/misc.h"

/* The data cache was reloaded from local core's L3 due to a demand load */
#define EventCode_1 0x1340000001c040
/* PM_DATA_RADIX_PROCESS_L2_PTE_FROM_L2 */
#define EventCode_2 0x14242
/* Event code with IFM, EBB, BHRB bits set in event code */
#define EventCode_3 0xf00000000000001e

/*
 * Some of the bits in the event code is
 * reserved for specific platforms.
 * Event code bits 52-59 are reserved in power9,
 * whereas in ISA v3.1, these are used for programming
 * Monitor Mode Control Register 3 (MMCR3).
 * Bit 9 in event code is reserved in power9,
 * whereas it is used for programming "radix_scope_qual"
 * bit 18 in Monitor Mode Control Register 1 (MMCR1).
 *
 * Testcase to ensure that using reserved bits in
 * event code should cause event_open to fail.
 */

static int invalid_event_code(void)
{
	struct event event;

	/* Check for platform support for the test */
	SKIP_IF(platform_check_for_tests());

	/*
	 * Events using MMCR3 bits and radix scope qual bits
	 * should fail in power9 and should succeed in power10 ( ISA v3.1 )
	 * Init the events and check for pass/fail in event open.
	 */
	if (have_hwcap2(PPC_FEATURE2_ARCH_3_1)) {
		event_init(&event, EventCode_1);
		FAIL_IF(event_open(&event));
		event_close(&event);

		event_init(&event, EventCode_2);
		FAIL_IF(event_open(&event));
		event_close(&event);
	} else {
		event_init(&event, EventCode_1);
		FAIL_IF(!event_open(&event));

		event_init(&event, EventCode_2);
		FAIL_IF(!event_open(&event));
	}

	return 0;
}

int main(void)
{
	return test_harness(invalid_event_code, "invalid_event_code");
}
