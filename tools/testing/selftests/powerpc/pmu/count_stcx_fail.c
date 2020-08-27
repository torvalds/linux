/*
 * Copyright 2013, Michael Ellerman, IBM Corp.
 * Licensed under GPLv2.
 */

#define _GNU_SOURCE

#include <stdio.h>
#include <stdbool.h>
#include <string.h>
#include <sys/prctl.h>

#include "event.h"
#include "utils.h"
#include "lib.h"

extern void thirty_two_instruction_loop_with_ll_sc(u64 loops, u64 *ll_sc_target);

static void setup_event(struct event *e, u64 config, int type, char *name)
{
	event_init_opts(e, config, type, name);

	e->attr.disabled = 1;
	e->attr.exclude_kernel = 1;
	e->attr.exclude_hv = 1;
	e->attr.exclude_idle = 1;
}

static int do_count_loop(struct event *events, u64 instructions,
			 u64 overhead, bool report)
{
	s64 difference, expected;
	double percentage;
	u64 dummy;

	prctl(PR_TASK_PERF_EVENTS_ENABLE);

	/* Run for 1M instructions */
	thirty_two_instruction_loop_with_ll_sc(instructions >> 5, &dummy);

	prctl(PR_TASK_PERF_EVENTS_DISABLE);

	event_read(&events[0]);
	event_read(&events[1]);
	event_read(&events[2]);

	expected = instructions + overhead + (events[2].result.value * 10);
	difference = events[0].result.value - expected;
	percentage = (double)difference / events[0].result.value * 100;

	if (report) {
		printf("-----\n");
		event_report(&events[0]);
		event_report(&events[1]);
		event_report(&events[2]);

		printf("Looped for %llu instructions, overhead %llu\n", instructions, overhead);
		printf("Expected %llu\n", expected);
		printf("Actual   %llu\n", events[0].result.value);
		printf("Delta    %lld, %f%%\n", difference, percentage);
	}

	event_reset(&events[0]);
	event_reset(&events[1]);
	event_reset(&events[2]);

	if (difference < 0)
		difference = -difference;

	/* Tolerate a difference below 0.0001 % */
	difference *= 10000 * 100;
	if (difference / events[0].result.value)
		return -1;

	return 0;
}

/* Count how many instructions it takes to do a null loop */
static u64 determine_overhead(struct event *events)
{
	u64 current, overhead;
	int i;

	do_count_loop(events, 0, 0, false);
	overhead = events[0].result.value;

	for (i = 0; i < 100; i++) {
		do_count_loop(events, 0, 0, false);
		current = events[0].result.value;
		if (current < overhead) {
			printf("Replacing overhead %llu with %llu\n", overhead, current);
			overhead = current;
		}
	}

	return overhead;
}

#define	PM_MRK_STCX_FAIL	0x03e158
#define PM_STCX_FAIL	0x01e058

static int test_body(void)
{
	struct event events[3];
	u64 overhead;

	setup_event(&events[0], PERF_COUNT_HW_INSTRUCTIONS, PERF_TYPE_HARDWARE, "instructions");
	setup_event(&events[1], PERF_COUNT_HW_CPU_CYCLES, PERF_TYPE_HARDWARE, "cycles");
	setup_event(&events[2], PM_STCX_FAIL, PERF_TYPE_RAW, "stcx_fail");

	if (event_open(&events[0])) {
		perror("perf_event_open");
		return -1;
	}

	if (event_open_with_group(&events[1], events[0].fd)) {
		perror("perf_event_open");
		return -1;
	}

	if (event_open_with_group(&events[2], events[0].fd)) {
		perror("perf_event_open");
		return -1;
	}

	overhead = determine_overhead(events);
	printf("Overhead of null loop: %llu instructions\n", overhead);

	/* Run for 1Mi instructions */
	FAIL_IF(do_count_loop(events, 1000000, overhead, true));

	/* Run for 10Mi instructions */
	FAIL_IF(do_count_loop(events, 10000000, overhead, true));

	/* Run for 100Mi instructions */
	FAIL_IF(do_count_loop(events, 100000000, overhead, true));

	/* Run for 1Bi instructions */
	FAIL_IF(do_count_loop(events, 1000000000, overhead, true));

	/* Run for 16Bi instructions */
	FAIL_IF(do_count_loop(events, 16000000000, overhead, true));

	/* Run for 64Bi instructions */
	FAIL_IF(do_count_loop(events, 64000000000, overhead, true));

	event_close(&events[0]);
	event_close(&events[1]);

	return 0;
}

static int count_ll_sc(void)
{
	return eat_cpu(test_body);
}

int main(void)
{
	return test_harness(count_ll_sc, "count_ll_sc");
}
