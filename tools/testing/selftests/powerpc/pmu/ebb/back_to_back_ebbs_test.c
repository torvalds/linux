/*
 * Copyright 2014, Michael Ellerman, IBM Corp.
 * Licensed under GPLv2.
 */

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>

#include "ebb.h"


#define NUMBER_OF_EBBS	50

/*
 * Test that if we overflow the counter while in the EBB handler, we take
 * another EBB on exiting from the handler.
 *
 * We do this by counting with a stupidly low sample period, causing us to
 * overflow the PMU while we're still in the EBB handler, leading to another
 * EBB.
 *
 * We get out of what would otherwise be an infinite loop by leaving the
 * counter frozen once we've taken enough EBBs.
 */

static void ebb_callee(void)
{
	uint64_t siar, val;

	val = mfspr(SPRN_BESCR);
	if (!(val & BESCR_PMEO)) {
		ebb_state.stats.spurious++;
		goto out;
	}

	ebb_state.stats.ebb_count++;
	trace_log_counter(ebb_state.trace, ebb_state.stats.ebb_count);

	/* Resets the PMC */
	count_pmc(1, sample_period);

out:
	if (ebb_state.stats.ebb_count == NUMBER_OF_EBBS)
		/* Reset but leave counters frozen */
		reset_ebb_with_clear_mask(MMCR0_PMAO);
	else
		/* Unfreezes */
		reset_ebb();

	/* Do some stuff to chew some cycles and pop the counter */
	siar = mfspr(SPRN_SIAR);
	trace_log_reg(ebb_state.trace, SPRN_SIAR, siar);

	val = mfspr(SPRN_PMC1);
	trace_log_reg(ebb_state.trace, SPRN_PMC1, val);

	val = mfspr(SPRN_MMCR0);
	trace_log_reg(ebb_state.trace, SPRN_MMCR0, val);
}

int back_to_back_ebbs(void)
{
	struct event event;

	SKIP_IF(!ebb_is_supported());

	event_init_named(&event, 0x1001e, "cycles");
	event_leader_ebb_init(&event);

	event.attr.exclude_kernel = 1;
	event.attr.exclude_hv = 1;
	event.attr.exclude_idle = 1;

	FAIL_IF(event_open(&event));

	setup_ebb_handler(ebb_callee);

	FAIL_IF(ebb_event_enable(&event));

	sample_period = 5;

	ebb_freeze_pmcs();
	mtspr(SPRN_PMC1, pmc_sample_period(sample_period));
	ebb_global_enable();
	ebb_unfreeze_pmcs();

	while (ebb_state.stats.ebb_count < NUMBER_OF_EBBS)
		FAIL_IF(core_busy_loop());

	ebb_global_disable();
	ebb_freeze_pmcs();

	dump_ebb_state();

	event_close(&event);

	FAIL_IF(ebb_state.stats.ebb_count != NUMBER_OF_EBBS);

	return 0;
}

int main(void)
{
	return test_harness(back_to_back_ebbs, "back_to_back_ebbs");
}
