/*
 * Copyright 2014, Michael Ellerman, IBM Corp.
 * Licensed under GPLv2.
 */

#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include <setjmp.h>

#include "ebb.h"


/*
 * Test that a fork clears the PMU state of the child. eg. BESCR/EBBHR/EBBRR
 * are cleared, and MMCR0_PMCC is reset, preventing the child from accessing
 * the PMU.
 */

static struct event event;

static int child(void)
{
	/* Even though we have EBE=0 we can still see the EBB regs */
	FAIL_IF(mfspr(SPRN_BESCR) != 0);
	FAIL_IF(mfspr(SPRN_EBBHR) != 0);
	FAIL_IF(mfspr(SPRN_EBBRR) != 0);

	FAIL_IF(catch_sigill(write_pmc1));

	/* We can still read from the event, though it is on our parent */
	FAIL_IF(event_read(&event));

	return 0;
}

/* Tests that fork clears EBB state */
int fork_cleanup(void)
{
	pid_t pid;

	SKIP_IF(!ebb_is_supported());

	event_init_named(&event, 0x1001e, "cycles");
	event_leader_ebb_init(&event);

	FAIL_IF(event_open(&event));

	ebb_enable_pmc_counting(1);
	setup_ebb_handler(standard_ebb_callee);
	ebb_global_enable();

	FAIL_IF(ebb_event_enable(&event));

	mtspr(SPRN_MMCR0, MMCR0_FC);
	mtspr(SPRN_PMC1, pmc_sample_period(sample_period));

	/* Don't need to actually take any EBBs */

	pid = fork();
	if (pid == 0)
		exit(child());

	/* Child does the actual testing */
	FAIL_IF(wait_for_child(pid));

	/* After fork */
	event_close(&event);

	return 0;
}

int main(void)
{
	return test_harness(fork_cleanup, "fork_cleanup");
}
