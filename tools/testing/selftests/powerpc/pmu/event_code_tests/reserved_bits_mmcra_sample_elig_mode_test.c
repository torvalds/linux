// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright 2022, Athira Rajeev, IBM Corp.
 */

#include <stdio.h>
#include "../event.h"
#include "../sampling_tests/misc.h"

/*
 * Testcase for reserved bits in Monitor Mode Control
 * Register A (MMCRA) Random Sampling Mode (SM) value.
 * As per Instruction Set Architecture (ISA), the values
 * 0x5, 0x9, 0xD, 0x19, 0x1D, 0x1A, 0x1E are reserved
 * for sampling mode field. Test that having these reserved
 * bit values should cause event_open to fail.
 * Input event code uses these sampling bits along with
 * 401e0 (PM_MRK_INST_CMPL).
 */

static int reserved_bits_mmcra_sample_elig_mode(void)
{
	struct event event;

	/* Check for platform support for the test */
	SKIP_IF(platform_check_for_tests());

	/* Skip for Generic compat PMU */
	SKIP_IF(check_for_generic_compat_pmu());

	/*
	 * MMCRA Random Sampling Mode (SM) values: 0x5
	 * 0x9, 0xD, 0x19, 0x1D, 0x1A, 0x1E is reserved.
	 * Expected to fail when using these reserved values.
	 */
	event_init(&event, 0x50401e0);
	FAIL_IF(!event_open(&event));

	event_init(&event, 0x90401e0);
	FAIL_IF(!event_open(&event));

	event_init(&event, 0xD0401e0);
	FAIL_IF(!event_open(&event));

	event_init(&event, 0x190401e0);
	FAIL_IF(!event_open(&event));

	event_init(&event, 0x1D0401e0);
	FAIL_IF(!event_open(&event));

	event_init(&event, 0x1A0401e0);
	FAIL_IF(!event_open(&event));

	event_init(&event, 0x1E0401e0);
	FAIL_IF(!event_open(&event));

	/*
	 * MMCRA Random Sampling Mode (SM) value 0x10
	 * is reserved in power10 and 0xC is reserved in
	 * power9.
	 */
	if (PVR_VER(mfspr(SPRN_PVR)) == POWER10) {
		event_init(&event, 0x100401e0);
		FAIL_IF(!event_open(&event));
	} else if (PVR_VER(mfspr(SPRN_PVR)) == POWER9) {
		event_init(&event, 0xC0401e0);
		FAIL_IF(!event_open(&event));
	}

	return 0;
}

int main(void)
{
	return test_harness(reserved_bits_mmcra_sample_elig_mode,
			    "reserved_bits_mmcra_sample_elig_mode");
}
