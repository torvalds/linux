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

/*
 * A perf sampling test for mmcra
 * field: bhrb_disable.
 */
static int mmcra_bhrb_disable_no_branch_test(void)
{
	struct event event;
	u64 *intr_regs;

	/*
	 * Check for platform support for the test.
	 * This test is only aplicable on ISA v3.1
	 */
	SKIP_IF(check_pvr_for_sampling_tests());
	SKIP_IF(!have_hwcap2(PPC_FEATURE2_ARCH_3_1));

	 /* Init the event for the sampling test */
	event_init_sampling(&event, EventCode);
	event.attr.sample_regs_intr = platform_extended_mask;
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

	/* Verify that bhrb_disable bit is set in MMCRA for non-branch samples */
	FAIL_IF(!get_mmcra_bhrb_disable(get_reg_value(intr_regs, "MMCRA"), 5));

	event_close(&event);
	return 0;
}

int main(void)
{
	return test_harness(mmcra_bhrb_disable_no_branch_test, "mmcra_bhrb_disable_no_branch_test");
}
