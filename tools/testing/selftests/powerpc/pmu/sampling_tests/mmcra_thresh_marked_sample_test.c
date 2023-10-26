// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright 2022, Kajol Jain, IBM Corp.
 */

#include <stdio.h>
#include <stdlib.h>

#include "../event.h"
#include "misc.h"
#include "utils.h"

/*
 * Primary PMU event used here is PM_MRK_INST_CMPL (0x401e0)
 * Threshold event selection used is issue to complete for cycles
 * Sampling criteria is Load only sampling
 */
#define EventCode 0x35340401e0

extern void thirty_two_instruction_loop_with_ll_sc(u64 loops, u64 *ll_sc_target);

/* A perf sampling test to test mmcra fields */
static int mmcra_thresh_marked_sample(void)
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
	thirty_two_instruction_loop_with_ll_sc(1000000, &dummy);

	FAIL_IF(event_disable(&event));

	/* Check for sample count */
	FAIL_IF(!collect_samples(event.mmap_buffer));

	intr_regs = get_intr_regs(&event, event.mmap_buffer);

	/* Check for intr_regs */
	FAIL_IF(!intr_regs);

	/*
	 * Verify that thresh sel/start/stop, marked, random sample
	 * eligibility, sdar mode and sample mode fields match with
	 * the corresponding event code fields
	 */
	FAIL_IF(EV_CODE_EXTRACT(event.attr.config, thd_sel) !=
			get_mmcra_thd_sel(get_reg_value(intr_regs, "MMCRA"), 4));
	FAIL_IF(EV_CODE_EXTRACT(event.attr.config, thd_start) !=
			get_mmcra_thd_start(get_reg_value(intr_regs, "MMCRA"), 4));
	FAIL_IF(EV_CODE_EXTRACT(event.attr.config, thd_stop) !=
			get_mmcra_thd_stop(get_reg_value(intr_regs, "MMCRA"), 4));
	FAIL_IF(EV_CODE_EXTRACT(event.attr.config, marked) !=
			get_mmcra_marked(get_reg_value(intr_regs, "MMCRA"), 4));
	FAIL_IF((EV_CODE_EXTRACT(event.attr.config, sample) >> 2) !=
			get_mmcra_rand_samp_elig(get_reg_value(intr_regs, "MMCRA"), 4));
	FAIL_IF((EV_CODE_EXTRACT(event.attr.config, sample) & 0x3) !=
			get_mmcra_sample_mode(get_reg_value(intr_regs, "MMCRA"), 4));
	FAIL_IF(EV_CODE_EXTRACT(event.attr.config, sm) !=
			get_mmcra_sm(get_reg_value(intr_regs, "MMCRA"), 4));

	event_close(&event);
	return 0;
}

int main(void)
{
	return test_harness(mmcra_thresh_marked_sample, "mmcra_thresh_marked_sample");
}
