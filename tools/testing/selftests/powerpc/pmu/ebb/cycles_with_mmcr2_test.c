// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright 2014, Michael Ellerman, IBM Corp.
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>

#include "ebb.h"


/*
 * Test of counting cycles while manipulating the user accessible bits in MMCR2.
 */

/* We use two values because the first freezes PMC1 and so we would get no EBBs */
#define MMCR2_EXPECTED_1 0x4020100804020000UL /* (FC1P|FC2P|FC3P|FC4P|FC5P|FC6P) */
#define MMCR2_EXPECTED_2 0x0020100804020000UL /* (     FC2P|FC3P|FC4P|FC5P|FC6P) */


int cycles_with_mmcr2(void)
{
	struct event event;
	uint64_t val, expected[2], actual;
	int i;
	bool bad_mmcr2;

	SKIP_IF(!ebb_is_supported());

	event_init_named(&event, 0x1001e, "cycles");
	event_leader_ebb_init(&event);

	event.attr.exclude_kernel = 1;
	event.attr.exclude_hv = 1;
	event.attr.exclude_idle = 1;

	FAIL_IF(event_open(&event));

	ebb_enable_pmc_counting(1);
	setup_ebb_handler(standard_ebb_callee);
	ebb_global_enable();

	FAIL_IF(ebb_event_enable(&event));

	mtspr(SPRN_PMC1, pmc_sample_period(sample_period));

	/* XXX Set of MMCR2 must be after enable */
	expected[0] = MMCR2_EXPECTED_1;
	expected[1] = MMCR2_EXPECTED_2;
	i = 0;
	bad_mmcr2 = false;

	/* Make sure we loop until we take at least one EBB */
	while ((ebb_state.stats.ebb_count < 20 && !bad_mmcr2) ||
		ebb_state.stats.ebb_count < 1)
	{
		mtspr(SPRN_MMCR2, expected[i % 2]);

		FAIL_IF(core_busy_loop());

		val = mfspr(SPRN_MMCR2);
		if (val != expected[i % 2]) {
			bad_mmcr2 = true;
			actual = val;
		}

		i++;
	}

	ebb_global_disable();
	ebb_freeze_pmcs();

	dump_ebb_state();

	event_close(&event);

	FAIL_IF(ebb_state.stats.ebb_count == 0);

	if (bad_mmcr2)
		printf("Bad MMCR2 value seen is 0x%lx\n", actual);

	FAIL_IF(bad_mmcr2);

	return 0;
}

int main(void)
{
	return test_harness(cycles_with_mmcr2, "cycles_with_mmcr2");
}
