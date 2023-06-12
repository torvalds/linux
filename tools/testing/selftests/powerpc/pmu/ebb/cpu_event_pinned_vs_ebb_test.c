// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright 2014, Michael Ellerman, IBM Corp.
 */

#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include "ebb.h"


/*
 * Tests a pinned cpu event vs an EBB - in that order. The pinned cpu event
 * should remain and the EBB event should fail to enable.
 */

static int setup_cpu_event(struct event *event, int cpu)
{
	event_init_named(event, 0x400FA, "PM_RUN_INST_CMPL");

	event->attr.pinned = 1;

	event->attr.exclude_kernel = 1;
	event->attr.exclude_hv = 1;
	event->attr.exclude_idle = 1;

	SKIP_IF(require_paranoia_below(1));
	FAIL_IF(event_open_with_cpu(event, cpu));
	FAIL_IF(event_enable(event));

	return 0;
}

int cpu_event_pinned_vs_ebb(void)
{
	union pipe read_pipe, write_pipe;
	struct event event;
	int cpu, rc;
	pid_t pid;

	SKIP_IF(!ebb_is_supported());

	cpu = bind_to_cpu(BIND_CPU_ANY);
	FAIL_IF(cpu < 0);

	FAIL_IF(pipe(read_pipe.fds) == -1);
	FAIL_IF(pipe(write_pipe.fds) == -1);

	pid = fork();
	if (pid == 0) {
		/* NB order of pipes looks reversed */
		exit(ebb_child(write_pipe, read_pipe));
	}

	/* We setup the cpu event first */
	rc = setup_cpu_event(&event, cpu);
	if (rc) {
		kill_child_and_wait(pid);
		return rc;
	}

	/* Signal the child to install its EBB event and wait */
	if (sync_with_child(read_pipe, write_pipe))
		/* If it fails, wait for it to exit */
		goto wait;

	/* Signal the child to run */
	FAIL_IF(sync_with_child(read_pipe, write_pipe));

wait:
	/* We expect it to fail to read the event */
	FAIL_IF(wait_for_child(pid) != 2);

	FAIL_IF(event_disable(&event));
	FAIL_IF(event_read(&event));

	event_report(&event);

	/* The cpu event should have run */
	FAIL_IF(event.result.value == 0);
	FAIL_IF(event.result.enabled != event.result.running);

	return 0;
}

int main(void)
{
	return test_harness(cpu_event_pinned_vs_ebb, "cpu_event_pinned_vs_ebb");
}
