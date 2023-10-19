// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright 2021, Athira Rajeev, IBM Corp.
 */

#include <stdio.h>
#include <stdlib.h>
#include <setjmp.h>
#include <signal.h>

#include "ebb.h"


/*
 * Test that closing the EBB event clears MMCR0_PMCC and
 * sets MMCR0_PMCCEXT preventing further read access to the
 * group B PMU registers.
 */

static int regs_access_pmccext(void)
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

	/*
	 * For ISA v3.1, verify the test takes a SIGILL when reading
	 * PMU regs after the event is closed. With the control bit
	 * in MMCR0 (PMCCEXT) restricting access to group B PMU regs,
	 * sigill is expected.
	 */
	if (have_hwcap2(PPC_FEATURE2_ARCH_3_1))
		FAIL_IF(catch_sigill(dump_ebb_state));
	else
		dump_ebb_state();

	return 0;
}

int main(void)
{
	return test_harness(regs_access_pmccext, "regs_access_pmccext");
}
