// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright 2014, Michael Ellerman, IBM Corp.
 */

#include <stdio.h>
#include <stdlib.h>
#include <setjmp.h>
#include <signal.h>

#include "ebb.h"


/*
 * Test that closing the EBB event clears MMCR0_PMCC, preventing further access
 * by userspace to the PMU hardware.
 */

int close_clears_pmcc(void)
{
	struct event event;

	SKIP_IF(!ebb_is_supported());

	event_init_named(&event, 0x1001e, "cycles");
	event_leader_ebb_init(&event);

	FAIL_IF(event_open(&event));

	ebb_enable_pmc_counting(1);
	setup_ebb_handler(standard_ebb_callee);
	ebb_global_enable();
	FAIL_IF(ebb_event_enable(&event));

	mtspr(SPRN_PMC1, pmc_sample_period(sample_period));

	while (ebb_state.stats.ebb_count < 1)
		FAIL_IF(core_busy_loop());

	ebb_global_disable();
	event_close(&event);

	FAIL_IF(ebb_state.stats.ebb_count == 0);

	/* The real test is here, do we take a SIGILL when writing PMU regs now
	 * that we have closed the event. We expect that we will. */

	FAIL_IF(catch_sigill(write_pmc1));

	/* We should still be able to read EBB regs though */
	mfspr(SPRN_EBBHR);
	mfspr(SPRN_EBBRR);
	mfspr(SPRN_BESCR);

	return 0;
}

int main(void)
{
	return test_harness(close_clears_pmcc, "close_clears_pmcc");
}
