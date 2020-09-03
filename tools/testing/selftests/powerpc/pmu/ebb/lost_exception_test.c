/*
 * Copyright 2014, Michael Ellerman, IBM Corp.
 * Licensed under GPLv2.
 */

#include <sched.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/mman.h>

#include "ebb.h"


/*
 * Test that tries to trigger CPU_FTR_PMAO_BUG. Which is a hardware defect
 * where an exception triggers but we context switch before it is delivered and
 * lose the exception.
 */

static int test_body(void)
{
	int i, orig_period, max_period;
	struct event event;

	SKIP_IF(!ebb_is_supported());

	/* We use PMC4 to make sure the kernel switches all counters correctly */
	event_init_named(&event, 0x40002, "instructions");
	event_leader_ebb_init(&event);

	event.attr.exclude_kernel = 1;
	event.attr.exclude_hv = 1;
	event.attr.exclude_idle = 1;

	FAIL_IF(event_open(&event));

	ebb_enable_pmc_counting(4);
	setup_ebb_handler(standard_ebb_callee);
	ebb_global_enable();
	FAIL_IF(ebb_event_enable(&event));

	/*
	 * We want a low sample period, but we also want to get out of the EBB
	 * handler without tripping up again.
	 *
	 * This value picked after much experimentation.
	 */
	orig_period = max_period = sample_period = 400;

	mtspr(SPRN_PMC4, pmc_sample_period(sample_period));

	while (ebb_state.stats.ebb_count < 1000000) {
		/*
		 * We are trying to get the EBB exception to race exactly with
		 * us entering the kernel to do the syscall. We then need the
		 * kernel to decide our timeslice is up and context switch to
		 * the other thread. When we come back our EBB will have been
		 * lost and we'll spin in this while loop forever.
		 */

		for (i = 0; i < 100000; i++)
			sched_yield();

		/* Change the sample period slightly to try and hit the race */
		if (sample_period >= (orig_period + 200))
			sample_period = orig_period;
		else
			sample_period++;

		if (sample_period > max_period)
			max_period = sample_period;
	}

	ebb_freeze_pmcs();
	ebb_global_disable();

	mtspr(SPRN_PMC4, 0xdead);

	dump_summary_ebb_state();
	dump_ebb_hw_state();

	event_close(&event);

	FAIL_IF(ebb_state.stats.ebb_count == 0);

	/* We vary our sample period so we need extra fudge here */
	FAIL_IF(!ebb_check_count(4, orig_period, 2 * (max_period - orig_period)));

	return 0;
}

static int lost_exception(void)
{
	return eat_cpu(test_body);
}

int main(void)
{
	test_harness_set_timeout(300);
	return test_harness(lost_exception, "lost_exception");
}
