// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright 2014, Michael Ellerman, IBM Corp.
 */

#define _GNU_SOURCE

#include <stdio.h>
#include <stdbool.h>
#include <string.h>
#include <sys/prctl.h>

#include "ebb.h"


/*
 * Run a calibrated instruction loop and count instructions executed using
 * EBBs. Make sure the counts look right.
 */

extern void thirty_two_instruction_loop(uint64_t loops);

static bool counters_frozen = true;

static int do_count_loop(struct event *event, uint64_t instructions,
			 uint64_t overhead, bool report)
{
	int64_t difference, expected;
	double percentage;

	clear_ebb_stats();

	counters_frozen = false;
	mb();
	mtspr(SPRN_MMCR0, mfspr(SPRN_MMCR0) & ~MMCR0_FC);

	thirty_two_instruction_loop(instructions >> 5);

	counters_frozen = true;
	mb();
	mtspr(SPRN_MMCR0, mfspr(SPRN_MMCR0) | MMCR0_FC);

	count_pmc(4, sample_period);

	event->result.value = ebb_state.stats.pmc_count[4-1];
	expected = instructions + overhead;
	difference = event->result.value - expected;
	percentage = (double)difference / event->result.value * 100;

	if (report) {
		printf("Looped for %lu instructions, overhead %lu\n", instructions, overhead);
		printf("Expected %lu\n", expected);
		printf("Actual   %llu\n", event->result.value);
		printf("Delta    %ld, %f%%\n", difference, percentage);
		printf("Took %d EBBs\n", ebb_state.stats.ebb_count);
	}

	if (difference < 0)
		difference = -difference;

	/* Tolerate a difference of up to 0.0001 % */
	difference *= 10000 * 100;
	if (difference / event->result.value)
		return -1;

	return 0;
}

/* Count how many instructions it takes to do a null loop */
static uint64_t determine_overhead(struct event *event)
{
	uint64_t current, overhead;
	int i;

	do_count_loop(event, 0, 0, false);
	overhead = event->result.value;

	for (i = 0; i < 100; i++) {
		do_count_loop(event, 0, 0, false);
		current = event->result.value;
		if (current < overhead) {
			printf("Replacing overhead %lu with %lu\n", overhead, current);
			overhead = current;
		}
	}

	return overhead;
}

static void pmc4_ebb_callee(void)
{
	uint64_t val;

	val = mfspr(SPRN_BESCR);
	if (!(val & BESCR_PMEO)) {
		ebb_state.stats.spurious++;
		goto out;
	}

	ebb_state.stats.ebb_count++;
	count_pmc(4, sample_period);
out:
	if (counters_frozen)
		reset_ebb_with_clear_mask(MMCR0_PMAO);
	else
		reset_ebb();
}

int instruction_count(void)
{
	struct event event;
	uint64_t overhead;

	SKIP_IF(!ebb_is_supported());

	event_init_named(&event, 0x400FA, "PM_RUN_INST_CMPL");
	event_leader_ebb_init(&event);
	event.attr.exclude_kernel = 1;
	event.attr.exclude_hv = 1;
	event.attr.exclude_idle = 1;

	FAIL_IF(event_open(&event));
	FAIL_IF(ebb_event_enable(&event));

	sample_period = COUNTER_OVERFLOW;

	setup_ebb_handler(pmc4_ebb_callee);
	mtspr(SPRN_MMCR0, mfspr(SPRN_MMCR0) & ~MMCR0_FC);
	ebb_global_enable();

	overhead = determine_overhead(&event);
	printf("Overhead of null loop: %lu instructions\n", overhead);

	/* Run for 1M instructions */
	FAIL_IF(do_count_loop(&event, 0x100000, overhead, true));

	/* Run for 10M instructions */
	FAIL_IF(do_count_loop(&event, 0xa00000, overhead, true));

	/* Run for 100M instructions */
	FAIL_IF(do_count_loop(&event, 0x6400000, overhead, true));

	/* Run for 1G instructions */
	FAIL_IF(do_count_loop(&event, 0x40000000, overhead, true));

	/* Run for 16G instructions */
	FAIL_IF(do_count_loop(&event, 0x400000000, overhead, true));

	/* Run for 64G instructions */
	FAIL_IF(do_count_loop(&event, 0x1000000000, overhead, true));

	/* Run for 128G instructions */
	FAIL_IF(do_count_loop(&event, 0x2000000000, overhead, true));

	ebb_global_disable();
	event_close(&event);

	printf("Finished OK\n");

	return 0;
}

int main(void)
{
	test_harness_set_timeout(300);
	return test_harness(instruction_count, "instruction_count");
}
