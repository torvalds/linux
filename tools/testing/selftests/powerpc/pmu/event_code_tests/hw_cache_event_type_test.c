// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright 2022, Kajol Jain, IBM Corp.
 */

#include <stdio.h>
#include <stdlib.h>

#include "../event.h"
#include "utils.h"
#include "../sampling_tests/misc.h"

/*
 * Load Missed L1, for power9 its pointing to PM_LD_MISS_L1_FIN (0x2c04e) and
 * for power10 its pointing to PM_LD_MISS_L1 (0x3e054)
 *
 * Hardware cache level : PERF_COUNT_HW_CACHE_L1D
 * Hardware cache event operation type : PERF_COUNT_HW_CACHE_OP_READ
 * Hardware cache event result type : PERF_COUNT_HW_CACHE_RESULT_MISS
 */
#define EventCode_1 0x10000
/*
 * Hardware cache level : PERF_COUNT_HW_CACHE_L1D
 * Hardware cache event operation type : PERF_COUNT_HW_CACHE_OP_WRITE
 * Hardware cache event result type : PERF_COUNT_HW_CACHE_RESULT_ACCESS
 */
#define EventCode_2 0x0100
/*
 * Hardware cache level : PERF_COUNT_HW_CACHE_DTLB
 * Hardware cache event operation type : PERF_COUNT_HW_CACHE_OP_WRITE
 * Hardware cache event result type : PERF_COUNT_HW_CACHE_RESULT_ACCESS
 */
#define EventCode_3 0x0103
/*
 * Hardware cache level : PERF_COUNT_HW_CACHE_L1D
 * Hardware cache event operation type : PERF_COUNT_HW_CACHE_OP_READ
 * Hardware cache event result type : Invalid ( > PERF_COUNT_HW_CACHE_RESULT_MAX)
 */
#define EventCode_4 0x030000

/*
 * A perf test to check valid hardware cache events.
 */
static int hw_cache_event_type_test(void)
{
	struct event event;

	/* Check for platform support for the test */
	SKIP_IF(platform_check_for_tests());

	/* Skip for Generic compat PMU */
	SKIP_IF(check_for_generic_compat_pmu());

	/* Init the event to test hardware cache event */
	event_init_opts(&event, EventCode_1, PERF_TYPE_HW_CACHE, "event");

	/* Expected to success as its pointing to L1 load miss */
	FAIL_IF(event_open(&event));
	event_close(&event);

	/* Init the event to test hardware cache event */
	event_init_opts(&event, EventCode_2, PERF_TYPE_HW_CACHE, "event");

	/* Expected to fail as the corresponding cache event entry have 0 in that index */
	FAIL_IF(!event_open(&event));
	event_close(&event);

	/* Init the event to test hardware cache event */
	event_init_opts(&event, EventCode_3, PERF_TYPE_HW_CACHE, "event");

	/* Expected to fail as the corresponding cache event entry have -1 in that index */
	FAIL_IF(!event_open(&event));
	event_close(&event);

	/* Init the event to test hardware cache event */
	event_init_opts(&event, EventCode_4, PERF_TYPE_HW_CACHE, "event");

	/* Expected to fail as hardware cache event result type is Invalid */
	FAIL_IF(!event_open(&event));
	event_close(&event);

	return 0;
}

int main(void)
{
	return test_harness(hw_cache_event_type_test, "hw_cache_event_type_test");
}
