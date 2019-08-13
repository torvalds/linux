// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright 2014, Michael Ellerman, IBM Corp.
 */

#define _GNU_SOURCE

#include <elf.h>
#include <limits.h>
#include <stdio.h>
#include <stdbool.h>
#include <string.h>
#include <sys/prctl.h>

#include "event.h"
#include "lib.h"
#include "utils.h"

/*
 * Test that per-event excludes work.
 */

static int per_event_excludes(void)
{
	struct event *e, events[4];
	char *platform;
	int i;

	platform = (char *)get_auxv_entry(AT_BASE_PLATFORM);
	FAIL_IF(!platform);
	SKIP_IF(strcmp(platform, "power8") != 0);

	/*
	 * We need to create the events disabled, otherwise the running/enabled
	 * counts don't match up.
	 */
	e = &events[0];
	event_init_opts(e, PERF_COUNT_HW_INSTRUCTIONS,
			PERF_TYPE_HARDWARE, "instructions");
	e->attr.disabled = 1;

	e = &events[1];
	event_init_opts(e, PERF_COUNT_HW_INSTRUCTIONS,
			PERF_TYPE_HARDWARE, "instructions(k)");
	e->attr.disabled = 1;
	e->attr.exclude_user = 1;
	e->attr.exclude_hv = 1;

	e = &events[2];
	event_init_opts(e, PERF_COUNT_HW_INSTRUCTIONS,
			PERF_TYPE_HARDWARE, "instructions(h)");
	e->attr.disabled = 1;
	e->attr.exclude_user = 1;
	e->attr.exclude_kernel = 1;

	e = &events[3];
	event_init_opts(e, PERF_COUNT_HW_INSTRUCTIONS,
			PERF_TYPE_HARDWARE, "instructions(u)");
	e->attr.disabled = 1;
	e->attr.exclude_hv = 1;
	e->attr.exclude_kernel = 1;

	FAIL_IF(event_open(&events[0]));

	/*
	 * The open here will fail if we don't have per event exclude support,
	 * because the second event has an incompatible set of exclude settings
	 * and we're asking for the events to be in a group.
	 */
	for (i = 1; i < 4; i++)
		FAIL_IF(event_open_with_group(&events[i], events[0].fd));

	/*
	 * Even though the above will fail without per-event excludes we keep
	 * testing in order to be thorough.
	 */
	prctl(PR_TASK_PERF_EVENTS_ENABLE);

	/* Spin for a while */
	for (i = 0; i < INT_MAX; i++)
		asm volatile("" : : : "memory");

	prctl(PR_TASK_PERF_EVENTS_DISABLE);

	for (i = 0; i < 4; i++) {
		FAIL_IF(event_read(&events[i]));
		event_report(&events[i]);
	}

	/*
	 * We should see that all events have enabled == running. That
	 * shows that they were all on the PMU at once.
	 */
	for (i = 0; i < 4; i++)
		FAIL_IF(events[i].result.running != events[i].result.enabled);

	/*
	 * We can also check that the result for instructions is >= all the
	 * other counts. That's because it is counting all instructions while
	 * the others are counting a subset.
	 */
	for (i = 1; i < 4; i++)
		FAIL_IF(events[0].result.value < events[i].result.value);

	for (i = 0; i < 4; i++)
		event_close(&events[i]);

	return 0;
}

int main(void)
{
	return test_harness(per_event_excludes, "per_event_excludes");
}
