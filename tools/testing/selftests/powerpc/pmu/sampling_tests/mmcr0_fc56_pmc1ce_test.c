// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright 2022, Athira Rajeev, IBM Corp.
 */

#include <stdio.h>
#include <stdlib.h>

#include "../event.h"
#include "misc.h"
#include "utils.h"

extern void thirty_two_instruction_loop(int loops);

/*
 * A perf sampling test for mmcr0
 * fields: fc56, pmc1ce.
 */
static int mmcr0_fc56_pmc1ce(void)
{
	struct event event;
	u64 *intr_regs;

	/* Check for platform support for the test */
	SKIP_IF(check_pvr_for_sampling_tests());

	/* Init the event for the sampling test */
	event_init_sampling(&event, 0x1001e);
	event.attr.sample_regs_intr = platform_extended_mask;
	FAIL_IF(event_open(&event));
	event.mmap_buffer = event_sample_buf_mmap(event.fd, 1);

	FAIL_IF(event_enable(&event));

	/* workload to make the event overflow */
	thirty_two_instruction_loop(10000);

	FAIL_IF(event_disable(&event));

	/* Check for sample count */
	FAIL_IF(!collect_samples(event.mmap_buffer));

	intr_regs = get_intr_regs(&event, event.mmap_buffer);

	/* Check for intr_regs */
	FAIL_IF(!intr_regs);

	/* Verify that fc56, pmc1ce fields are set in MMCR0 */
	FAIL_IF(!get_mmcr0_fc56(get_reg_value(intr_regs, "MMCR0"), 1));
	FAIL_IF(!get_mmcr0_pmc1ce(get_reg_value(intr_regs, "MMCR0"), 1));

	event_close(&event);
	return 0;
}

int main(void)
{
	return test_harness(mmcr0_fc56_pmc1ce, "mmcr0_fc56_pmc1ce");
}
