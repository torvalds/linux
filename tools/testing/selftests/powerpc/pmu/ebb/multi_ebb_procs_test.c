// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright 2014, Michael Ellerman, IBM Corp.
 */

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <signal.h>

#include "ebb.h"


/*
 * Test running multiple EBB using processes at once on a single CPU. They
 * should all run happily without interfering with each other.
 */

static bool child_should_exit;

static void sigint_handler(int signal)
{
	child_should_exit = true;
}

struct sigaction sigint_action = {
	.sa_handler = sigint_handler,
};

static int cycles_child(void)
{
	struct event event;

	if (sigaction(SIGINT, &sigint_action, NULL)) {
		perror("sigaction");
		return 1;
	}

	event_init_named(&event, 0x1001e, "cycles");
	event_leader_ebb_init(&event);

	event.attr.exclude_kernel = 1;
	event.attr.exclude_hv = 1;
	event.attr.exclude_idle = 1;

	FAIL_IF(event_open(&event));

	ebb_enable_pmc_counting(1);
	setup_ebb_handler(standard_ebb_callee);
	ebb_global_enable();

	FAIL_IF(ebb_event_enable(&event));

	mtspr(SPRN_PMC1, pmc_sample_period(sample_period));

	while (!child_should_exit) {
		FAIL_IF(core_busy_loop());
		FAIL_IF(ebb_check_mmcr0());
	}

	ebb_global_disable();
	ebb_freeze_pmcs();

	dump_summary_ebb_state();

	event_close(&event);

	FAIL_IF(ebb_state.stats.ebb_count == 0);

	return 0;
}

#define NR_CHILDREN	4

int multi_ebb_procs(void)
{
	pid_t pids[NR_CHILDREN];
	int rc, i;

	SKIP_IF(!ebb_is_supported());

	FAIL_IF(bind_to_cpu(BIND_CPU_ANY) < 0);

	for (i = 0; i < NR_CHILDREN; i++) {
		pids[i] = fork();
		if (pids[i] == 0)
			exit(cycles_child());
	}

	/* Have them all run for "a while" */
	sleep(10);

	rc = 0;
	for (i = 0; i < NR_CHILDREN; i++) {
		/* Tell them to stop */
		kill(pids[i], SIGINT);
		/* And wait */
		rc |= wait_for_child(pids[i]);
	}

	return rc;
}

int main(void)
{
	return test_harness(multi_ebb_procs, "multi_ebb_procs");
}
