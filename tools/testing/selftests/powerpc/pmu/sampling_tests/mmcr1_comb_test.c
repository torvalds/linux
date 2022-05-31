// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright 2022, Athira Rajeev, IBM Corp.
 */

#include <stdio.h>
#include <stdlib.h>

#include "../event.h"
#include "misc.h"
#include "utils.h"

/* All successful D-side store dispatches for this thread that were L2 Miss */
#define EventCode 0x46880

extern void thirty_two_instruction_loop_with_ll_sc(u64 loops, u64 *ll_sc_target);

/*
 * A perf sampling test for mmcr1
 * fields : comb.
 */
static int mmcr1_comb(void)
{
	struct event event;
	u64 *intr_regs;
	u64 dummy;

	/* Check for platform support for the test */
	SKIP_IF(check_pvr_for_sampling_tests());

	/* Init the event for the sampling test */
	event_init_sampling(&event, EventCode);
	event.attr.sample_regs_intr = platform_extended_mask;
	FAIL_IF(event_open(&event));
	event.mmap_buffer = event_sample_buf_mmap(event.fd, 1);

	FAIL_IF(event_enable(&event));

	/* workload to make the event overflow */
	thirty_two_instruction_loop_with_ll_sc(10000000, &dummy);

	FAIL_IF(event_disable(&event));

	/* Check for sample count */
	FAIL_IF(!collect_samples(event.mmap_buffer));

	intr_regs = get_intr_regs(&event, event.mmap_buffer);

	/* Check for intr_regs */
	FAIL_IF(!intr_regs);

	/*
	 * Verify that comb field match with
	 * corresponding event code fields
	 */
	FAIL_IF(EV_CODE_EXTRACT(event.attr.config, comb) !=
		get_mmcr1_comb(get_reg_value(intr_regs, "MMCR1"), 4));

	event_close(&event);
	return 0;
}

int main(void)
{
	return test_harness(mmcr1_comb, "mmcr1_comb");
}
