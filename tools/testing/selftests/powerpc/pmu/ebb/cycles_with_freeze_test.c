// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright 2014, Michael Ellerman, IBM Corp.
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>

#include "ebb.h"


/*
 * Test of counting cycles while using MMCR0_FC (freeze counters) to only count
 * parts of the code. This is complicated by the fact that FC is set by the
 * hardware when the event overflows. We may take the EBB after we have set FC,
 * so we have to be careful about whether we clear FC at the end of the EBB
 * handler or not.
 */

static bool counters_frozen = false;
static int ebbs_while_frozen = 0;

static void ebb_callee(void)
{
	uint64_t mask, val;

	mask = MMCR0_PMAO | MMCR0_FC;

	val = mfspr(SPRN_BESCR);
	if (!(val & BESCR_PMEO)) {
		ebb_state.stats.spurious++;
		goto out;
	}

	ebb_state.stats.ebb_count++;
	trace_log_counter(ebb_state.trace, ebb_state.stats.ebb_count);

	val = mfspr(SPRN_MMCR0);
	trace_log_reg(ebb_state.trace, SPRN_MMCR0, val);

	if (counters_frozen) {
		trace_log_string(ebb_state.trace, "frozen");
		ebbs_while_frozen++;
		mask &= ~MMCR0_FC;
	}

	count_pmc(1, sample_period);
out:
	reset_ebb_with_clear_mask(mask);
}

int cycles_with_freeze(void)
{
	struct event event;
	uint64_t val;
	bool fc_cleared;

	SKIP_IF(!ebb_is_supported());

	event_init_named(&event, 0x1001e, "cycles");
	event_leader_ebb_init(&event);

	event.attr.exclude_kernel = 1;
	event.attr.exclude_hv = 1;
	event.attr.exclude_idle = 1;

	FAIL_IF(event_open(&event));

	setup_ebb_handler(ebb_callee);
	ebb_global_enable();
	FAIL_IF(ebb_event_enable(&event));

	mtspr(SPRN_PMC1, pmc_sample_period(sample_period));

	fc_cleared = false;

	/* Make sure we loop until we take at least one EBB */
	while ((ebb_state.stats.ebb_count < 20 && !fc_cleared) ||
		ebb_state.stats.ebb_count < 1)
	{
		counters_frozen = false;
		mb();
		mtspr(SPRN_MMCR0, mfspr(SPRN_MMCR0) & ~MMCR0_FC);

		FAIL_IF(core_busy_loop());

		counters_frozen = true;
		mb();
		mtspr(SPRN_MMCR0, mfspr(SPRN_MMCR0) |  MMCR0_FC);

		val = mfspr(SPRN_MMCR0);
		if (! (val & MMCR0_FC)) {
			printf("Outside of loop, FC NOT set MMCR0 0x%lx\n", val);
			fc_cleared = true;
		}
	}

	ebb_global_disable();
	ebb_freeze_pmcs();

	dump_ebb_state();

	printf("EBBs while frozen %d\n", ebbs_while_frozen);

	event_close(&event);

	FAIL_IF(ebb_state.stats.ebb_count == 0);
	FAIL_IF(fc_cleared);

	return 0;
}

int main(void)
{
	return test_harness(cycles_with_freeze, "cycles_with_freeze");
}
