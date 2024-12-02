// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright 2022, Athira Rajeev, IBM Corp.
 */

#include <stdio.h>
#include <sys/prctl.h>
#include <limits.h>
#include "../event.h"
#include "../sampling_tests/misc.h"

/*
 * Testcase to ensure that using invalid event in generic
 * event for PERF_TYPE_HARDWARE should fail
 */

static int generic_events_valid_test(void)
{
	struct event event;

	/* Check for platform support for the test */
	SKIP_IF(platform_check_for_tests());

	/* generic events is different in compat_mode */
	SKIP_IF(check_for_generic_compat_pmu());

	/*
	 * Invalid generic events in power10:
	 * - PERF_COUNT_HW_BUS_CYCLES
	 * - PERF_COUNT_HW_STALLED_CYCLES_FRONTEND
	 * - PERF_COUNT_HW_STALLED_CYCLES_BACKEND
	 * - PERF_COUNT_HW_REF_CPU_CYCLES
	 */
	if (PVR_VER(mfspr(SPRN_PVR)) == POWER10) {
		event_init_opts(&event, PERF_COUNT_HW_CPU_CYCLES, PERF_TYPE_HARDWARE, "event");
		FAIL_IF(event_open(&event));
		event_close(&event);

		event_init_opts(&event, PERF_COUNT_HW_INSTRUCTIONS,
				PERF_TYPE_HARDWARE, "event");
		FAIL_IF(event_open(&event));
		event_close(&event);

		event_init_opts(&event, PERF_COUNT_HW_CACHE_REFERENCES,
				PERF_TYPE_HARDWARE, "event");
		FAIL_IF(event_open(&event));
		event_close(&event);

		event_init_opts(&event, PERF_COUNT_HW_CACHE_MISSES, PERF_TYPE_HARDWARE, "event");
		FAIL_IF(event_open(&event));
		event_close(&event);

		event_init_opts(&event, PERF_COUNT_HW_BRANCH_INSTRUCTIONS,
				PERF_TYPE_HARDWARE, "event");
		FAIL_IF(event_open(&event));
		event_close(&event);

		event_init_opts(&event, PERF_COUNT_HW_BRANCH_MISSES, PERF_TYPE_HARDWARE, "event");
		FAIL_IF(event_open(&event));
		event_close(&event);

		event_init_opts(&event, PERF_COUNT_HW_BUS_CYCLES, PERF_TYPE_HARDWARE, "event");
		FAIL_IF(!event_open(&event));

		event_init_opts(&event, PERF_COUNT_HW_STALLED_CYCLES_FRONTEND,
				PERF_TYPE_HARDWARE, "event");
		FAIL_IF(!event_open(&event));

		event_init_opts(&event, PERF_COUNT_HW_STALLED_CYCLES_BACKEND,
				PERF_TYPE_HARDWARE, "event");
		FAIL_IF(!event_open(&event));

		event_init_opts(&event, PERF_COUNT_HW_REF_CPU_CYCLES, PERF_TYPE_HARDWARE, "event");
		FAIL_IF(!event_open(&event));
	} else if (PVR_VER(mfspr(SPRN_PVR)) == POWER9) {
		/*
		 * Invalid generic events in power9:
		 * - PERF_COUNT_HW_BUS_CYCLES
		 * - PERF_COUNT_HW_REF_CPU_CYCLES
		 */
		event_init_opts(&event, PERF_COUNT_HW_CPU_CYCLES, PERF_TYPE_HARDWARE, "event");
		FAIL_IF(event_open(&event));
		event_close(&event);

		event_init_opts(&event, PERF_COUNT_HW_INSTRUCTIONS, PERF_TYPE_HARDWARE, "event");
		FAIL_IF(event_open(&event));
		event_close(&event);

		event_init_opts(&event, PERF_COUNT_HW_CACHE_REFERENCES,
				PERF_TYPE_HARDWARE, "event");
		FAIL_IF(event_open(&event));
		event_close(&event);

		event_init_opts(&event, PERF_COUNT_HW_CACHE_MISSES, PERF_TYPE_HARDWARE, "event");
		FAIL_IF(event_open(&event));
		event_close(&event);

		event_init_opts(&event, PERF_COUNT_HW_BRANCH_INSTRUCTIONS,
				PERF_TYPE_HARDWARE, "event");
		FAIL_IF(event_open(&event));
		event_close(&event);

		event_init_opts(&event, PERF_COUNT_HW_BRANCH_MISSES, PERF_TYPE_HARDWARE, "event");
		FAIL_IF(event_open(&event));
		event_close(&event);

		event_init_opts(&event, PERF_COUNT_HW_BUS_CYCLES, PERF_TYPE_HARDWARE, "event");
		FAIL_IF(!event_open(&event));

		event_init_opts(&event, PERF_COUNT_HW_STALLED_CYCLES_FRONTEND,
				PERF_TYPE_HARDWARE, "event");
		FAIL_IF(event_open(&event));
		event_close(&event);

		event_init_opts(&event, PERF_COUNT_HW_STALLED_CYCLES_BACKEND,
				PERF_TYPE_HARDWARE, "event");
		FAIL_IF(event_open(&event));
		event_close(&event);

		event_init_opts(&event, PERF_COUNT_HW_REF_CPU_CYCLES, PERF_TYPE_HARDWARE, "event");
		FAIL_IF(!event_open(&event));
	}

	return 0;
}

int main(void)
{
	return test_harness(generic_events_valid_test, "generic_events_valid_test");
}
