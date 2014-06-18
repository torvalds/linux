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

#include "ebb.h"


/*
 * Tests a per-task event vs an EBB - in that order. The EBB should push the
 * per-task event off the PMU.
 */

static int setup_child_event(struct event *event, pid_t child_pid)
{
	event_init_named(event, 0x400FA, "PM_RUN_INST_CMPL");

	event->attr.exclude_kernel = 1;
	event->attr.exclude_hv = 1;
	event->attr.exclude_idle = 1;

	FAIL_IF(event_open_with_pid(event, child_pid));
	FAIL_IF(event_enable(event));

	return 0;
}

int task_event_vs_ebb(void)
{
	union pipe read_pipe, write_pipe;
	struct event event;
	pid_t pid;
	int rc;

	FAIL_IF(pipe(read_pipe.fds) == -1);
	FAIL_IF(pipe(write_pipe.fds) == -1);

	pid = fork();
	if (pid == 0) {
		/* NB order of pipes looks reversed */
		exit(ebb_child(write_pipe, read_pipe));
	}

	/* We setup the task event first */
	rc = setup_child_event(&event, pid);
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
	/* The EBB event should push the task event off so the child should succeed */
	FAIL_IF(wait_for_child(pid));
	FAIL_IF(event_disable(&event));
	FAIL_IF(event_read(&event));

	event_report(&event);

	/* The task event may have run, or not so we can't assert anything about it */

	return 0;
}

int main(void)
{
	return test_harness(task_event_vs_ebb, "task_event_vs_ebb");
}
