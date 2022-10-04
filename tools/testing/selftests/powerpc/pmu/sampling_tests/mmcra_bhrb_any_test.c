// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright 2022, Kajol Jain, IBM Corp.
 */

#include <stdio.h>
#include <stdlib.h>

#include "../event.h"
#include "misc.h"
#include "utils.h"

extern void thirty_two_instruction_loop(int loops);

/* Instructions */
#define EventCode 0x500fa

/* ifm field for any branch mode */
#define IFM_ANY_BRANCH 0x0

/*
 * A perf sampling test for mmcra
 * field: ifm for bhrb any call.
 */
static int mmcra_bhrb_any_test(void)
{
	struct event event;
	u64 *intr_regs;

	/* Check for platform support for the test */
	SKIP_IF(check_pvr_for_sampling_tests());

	 /* Init the event for the sampling test */
	event_init_sampling(&event, EventCode);
	event.attr.sample_regs_intr = platform_extended_mask;
	event.attr.sample_type |= PERF_SAMPLE_BRANCH_STACK;
	event.attr.branch_sample_type = PERF_SAMPLE_BRANCH_ANY;
	event.attr.exclude_kernel = 1;

	FAIL_IF(event_open(&event));
	event.mmap_buffer = event_sample_buf_mmap(event.fd, 1);

	FAIL_IF(event_enable(&event));

	/* workload to make the event overflow */
	thirty_two_instruction_loop(10000);

	FAIL_IF(event_disable(&event));

	intr_regs = get_intr_regs(&event, event.mmap_buffer);

	/* Check for intr_regs */
	FAIL_IF(!intr_regs);

	/* Verify that ifm bit is set properly in MMCRA */
	FAIL_IF(get_mmcra_ifm(get_reg_value(intr_regs, "MMCRA"), 5) != IFM_ANY_BRANCH);

	event_close(&event);
	return 0;
}

int main(void)
{
	return test_harness(mmcra_bhrb_any_test, "mmcra_bhrb_any_test");
}
