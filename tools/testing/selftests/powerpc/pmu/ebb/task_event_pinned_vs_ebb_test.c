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
 * Tests a pinned per-task event vs an EBB - in that order. The pinned per-task
 * event should prevent the EBB event from being enabled.
 */

static int setup_child_event(struct event *event, pid_t child_pid)
{
	event_init_named(event, 0x400FA, "PM_RUN_INST_CMPL");

	event->attr.pinned = 1;

	event->attr.exclude_kernel = 1;
	event->attr.exclude_hv = 1;
	event->attr.exclude_idle = 1;

	FAIL_IF(event_open_with_pid(event, child_pid));
	FAIL_IF(event_enable(event));

	return 0;
}

int task_event_pinned_vs_ebb(void)
{
	union pipe read_pipe, write_pipe;
	struct event event;
	pid_t pid;
	int rc;

	SKIP_IF(!ebb_is_supported());

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
	/* We expect it to fail to read the event */
	FAIL_IF(wait_for_child(pid) != 2);
	FAIL_IF(event_disable(&event));
	FAIL_IF(event_read(&event));

	event_report(&event);

	FAIL_IF(event.result.value == 0);
	/*
	 * For reasons I don't understand enabled is usually just slightly
	 * lower than running. Would be good to confirm why.
	 */
	FAIL_IF(event.result.enabled == 0);
	FAIL_IF(event.result.running == 0);

	return 0;
}

int main(void)
{
	return test_harness(task_event_pinned_vs_ebb, "task_event_pinned_vs_ebb");
}
