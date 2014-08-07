/*
 * Copyright 2014, Michael Ellerman, IBM Corp.
 * Licensed under GPLv2.
 */

#include <stdio.h>
#include <stdlib.h>

#include "ebb.h"


/*
 * Basic test that counts user cycles and takes EBBs.
 */
int cycles(void)
{
	struct event event;

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

	while (ebb_state.stats.ebb_count < 10) {
		FAIL_IF(core_busy_loop());
		FAIL_IF(ebb_check_mmcr0());
	}

	ebb_global_disable();
	ebb_freeze_pmcs();

	count_pmc(1, sample_period);

	dump_ebb_state();

	event_close(&event);

	FAIL_IF(ebb_state.stats.ebb_count == 0);
	FAIL_IF(!ebb_check_count(1, sample_period, 100));

	return 0;
}

int main(void)
{
	return test_harness(cycles, "cycles");
}
