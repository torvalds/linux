// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2021, Oracle and/or its affiliates. */

#include <test_progs.h>

/* Test that verifies exception handling is working. fork()
 * triggers task_newtask tracepoint; that new task will have a
 * NULL pointer task_works, and the associated task->task_works->func
 * should not be NULL if task_works itself is non-NULL.
 *
 * So to verify exception handling we want to see a NULL task_works
 * and task_works->func; if we see this we can conclude that the
 * exception handler ran when we attempted to dereference task->task_works
 * and zeroed the destination register.
 */
#include "exhandler_kern.skel.h"

void test_exhandler(void)
{
	int err = 0, duration = 0, status;
	struct exhandler_kern *skel;
	pid_t cpid;

	skel = exhandler_kern__open_and_load();
	if (CHECK(!skel, "skel_load", "skeleton failed: %d\n", err))
		goto cleanup;

	skel->bss->test_pid = getpid();

	err = exhandler_kern__attach(skel);
	if (!ASSERT_OK(err, "attach"))
		goto cleanup;
	cpid = fork();
	if (!ASSERT_GT(cpid, -1, "fork failed"))
		goto cleanup;
	if (cpid == 0)
		_exit(0);
	waitpid(cpid, &status, 0);

	ASSERT_NEQ(skel->bss->exception_triggered, 0, "verify exceptions occurred");
cleanup:
	exhandler_kern__destroy(skel);
}
