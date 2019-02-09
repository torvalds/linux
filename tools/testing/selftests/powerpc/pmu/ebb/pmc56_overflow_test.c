/*
 * Copyright 2014, Michael Ellerman, IBM Corp.
 * Licensed under GPLv2.
 */

#include <stdio.h>
#include <stdlib.h>

#include "ebb.h"


/*
 * Test that PMC5 & 6 are frozen (ie. don't overflow) when they are not being
 * used. Tests the MMCR0_FC56 logic in the kernel.
 */

static int pmc56_overflowed;

static void ebb_callee(void)
{
	uint64_t val;

	val = mfspr(SPRN_BESCR);
	if (!(val & BESCR_PMEO)) {
		ebb_state.stats.spurious++;
		goto out;
	}

	ebb_state.stats.ebb_count++;
	count_pmc(2, sample_period);

	val = mfspr(SPRN_PMC5);
	if (val >= COUNTER_OVERFLOW)
		pmc56_overflowed++;

	count_pmc(5, COUNTER_OVERFLOW);

	val = mfspr(SPRN_PMC6);
	if (val >= COUNTER_OVERFLOW)
		pmc56_overflowed++;

	count_pmc(6, COUNTER_OVERFLOW);

out:
	reset_ebb();
}

int pmc56_overflow(void)
{
	struct event event;

	SKIP_IF(!ebb_is_supported());

	/* Use PMC2 so we set PMCjCE, which enables PMC5/6 */
	event_init(&event, 0x2001e);
	event_leader_ebb_init(&event);

	event.attr.exclude_kernel = 1;
	event.attr.exclude_hv = 1;
	event.attr.exclude_idle = 1;

	FAIL_IF(event_open(&event));

	setup_ebb_handler(ebb_callee);
	ebb_global_enable();

	FAIL_IF(ebb_event_enable(&event));

	mtspr(SPRN_PMC2, pmc_sample_period(sample_period));
	mtspr(SPRN_PMC5, 0);
	mtspr(SPRN_PMC6, 0);

	while (ebb_state.stats.ebb_count < 10)
		FAIL_IF(core_busy_loop());

	ebb_global_disable();
	ebb_freeze_pmcs();

	count_pmc(2, sample_period);

	dump_ebb_state();

	printf("PMC5/6 overflow %d\n", pmc56_overflowed);

	event_close(&event);

	FAIL_IF(ebb_state.stats.ebb_count == 0 || pmc56_overflowed != 0);

	return 0;
}

int main(void)
{
	return test_harness(pmc56_overflow, "pmc56_overflow");
}
