// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright 2022, Madhavan Srinivasan, IBM Corp.
 */

#include <stdio.h>
#include <stdlib.h>

#include "../event.h"
#include "misc.h"
#include "utils.h"

/* All successful D-side store dispatches for this thread */
#define EventCode 0x010000046080

#define MALLOC_SIZE     (0x10000 * 10)  /* Ought to be enough .. */

/*
 * A perf sampling test for mmcr2
 * fields : l2l3
 */
static int mmcr2_l2l3(void)
{
	struct event event;
	u64 *intr_regs;
	char *p;
	int i;

	/* Check for platform support for the test */
	SKIP_IF(check_pvr_for_sampling_tests());
	SKIP_IF(!have_hwcap2(PPC_FEATURE2_ARCH_3_1));

	/* Init the event for the sampling test */
	event_init_sampling(&event, EventCode);
	event.attr.sample_regs_intr = platform_extended_mask;
	FAIL_IF(event_open(&event));
	event.mmap_buffer = event_sample_buf_mmap(event.fd, 1);

	FAIL_IF(event_enable(&event));

	/* workload to make the event overflow */
	p = malloc(MALLOC_SIZE);
	FAIL_IF(!p);

	for (i = 0; i < MALLOC_SIZE; i += 0x10000)
		p[i] = i;

	FAIL_IF(event_disable(&event));

	/* Check for sample count */
	FAIL_IF(!collect_samples(event.mmap_buffer));

	intr_regs = get_intr_regs(&event, event.mmap_buffer);

	/* Check for intr_regs */
	FAIL_IF(!intr_regs);

	/*
	 * Verify that l2l3 field of MMCR2 match with
	 * corresponding event code field
	 */
	FAIL_IF(EV_CODE_EXTRACT(event.attr.config, l2l3) !=
		get_mmcr2_l2l3(get_reg_value(intr_regs, "MMCR2"), 4));

	event_close(&event);
	free(p);

	return 0;
}

int main(void)
{
	return test_harness(mmcr2_l2l3, "mmcr2_l2l3");
}
