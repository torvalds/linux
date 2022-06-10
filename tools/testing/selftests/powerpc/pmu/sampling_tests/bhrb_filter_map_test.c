// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright 2022, Athira Rajeev, IBM Corp.
 */

#include <stdio.h>
#include <stdlib.h>

#include "../event.h"
#include "misc.h"
#include "utils.h"

/*
 * A perf sampling test to check bhrb filter
 * map. All the branch filters are not supported
 * in powerpc. Supported filters in:
 * power10: any, any_call, ind_call, cond
 * power9: any, any_call
 *
 * Testcase checks event open for invalid bhrb filter
 * types should fail and valid filter types should pass.
 * Testcase does validity check for these branch
 * sample types.
 */

/* Invalid types for powerpc */
/* Valid bhrb filters in power9/power10 */
int bhrb_filter_map_valid_common[] = {
	PERF_SAMPLE_BRANCH_ANY,
	PERF_SAMPLE_BRANCH_ANY_CALL,
};

/* Valid bhrb filters in power10 */
int bhrb_filter_map_valid_p10[] = {
	PERF_SAMPLE_BRANCH_IND_CALL,
	PERF_SAMPLE_BRANCH_COND,
};

#define EventCode 0x1001e

static int bhrb_filter_map_test(void)
{
	struct event event;
	int i;

	/* Check for platform support for the test */
	SKIP_IF(platform_check_for_tests());

	/*
	 * Skip for Generic compat PMU since
	 * bhrb filters is not supported
	 */
	SKIP_IF(check_for_generic_compat_pmu());

	/* Init the event for the sampling test */
	event_init(&event, EventCode);

	event.attr.sample_period = 1000;
	event.attr.sample_type = PERF_SAMPLE_BRANCH_STACK;
	event.attr.disabled = 1;

	/* Invalid filter maps which are expected to fail in event_open */
	for (i = PERF_SAMPLE_BRANCH_USER_SHIFT; i < PERF_SAMPLE_BRANCH_MAX_SHIFT; i++) {
		/* Skip the valid branch sample type */
		if (i == PERF_SAMPLE_BRANCH_ANY_SHIFT || i == PERF_SAMPLE_BRANCH_ANY_CALL_SHIFT \
			|| i == PERF_SAMPLE_BRANCH_IND_CALL_SHIFT || i == PERF_SAMPLE_BRANCH_COND_SHIFT)
			continue;
		event.attr.branch_sample_type = 1U << i;
		FAIL_IF(!event_open(&event));
	}

	/* valid filter maps for power9/power10 which are expected to pass in event_open */
	for (i = 0; i < ARRAY_SIZE(bhrb_filter_map_valid_common); i++) {
		event.attr.branch_sample_type = bhrb_filter_map_valid_common[i];
		FAIL_IF(event_open(&event));
		event_close(&event);
	}

	/*
	 * filter maps which are valid in power10 and invalid in power9.
	 * PVR check is used here since PMU specific data like bhrb filter
	 * alternative tests is handled by respective PMU driver code and
	 * using PVR will work correctly for all cases including generic
	 * compat mode.
	 */
	if (PVR_VER(mfspr(SPRN_PVR)) == POWER10) {
		for (i = 0; i < ARRAY_SIZE(bhrb_filter_map_valid_p10); i++) {
			event.attr.branch_sample_type = bhrb_filter_map_valid_p10[i];
			FAIL_IF(event_open(&event));
			event_close(&event);
		}
	} else {
		for (i = 0; i < ARRAY_SIZE(bhrb_filter_map_valid_p10); i++) {
			event.attr.branch_sample_type = bhrb_filter_map_valid_p10[i];
			FAIL_IF(!event_open(&event));
		}
	}

	return 0;
}

int main(void)
{
	return test_harness(bhrb_filter_map_test, "bhrb_filter_map_test");
}
