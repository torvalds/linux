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
 * fields : pmae, pmao.
 */
static int mmcr0_exceptionbits(void)
{
	struct event event;
	u64 *intr_regs;

	/* Check for platform support for the test */
	SKIP_IF(check_pvr_for_sampling_tests());

	/* Init the event for the sampling test */
	event_init_sampling(&event, 0x500fa);
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

	/* Verify that pmae is cleared and pmao is set in MMCR0 */
	FAIL_IF(get_mmcr0_pmae(get_reg_value(intr_regs, "MMCR0"), 5));
	FAIL_IF(!get_mmcr0_pmao(get_reg_value(intr_regs, "MMCR0"), 5));

	event_close(&event);
	return 0;
}

int main(void)
{
	return test_harness(mmcr0_exceptionbits, "mmcr0_exceptionbits");
}
