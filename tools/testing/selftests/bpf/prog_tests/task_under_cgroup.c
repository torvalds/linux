// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2023 Bytedance */

#include <sys/syscall.h>
#include <test_progs.h>
#include <cgroup_helpers.h>
#include "test_task_under_cgroup.skel.h"

#define FOO	"/foo"

void test_task_under_cgroup(void)
{
	struct test_task_under_cgroup *skel;
	int ret, foo;
	pid_t pid;

	foo = test__join_cgroup(FOO);
	if (!ASSERT_OK(foo < 0, "cgroup_join_foo"))
		return;

	skel = test_task_under_cgroup__open();
	if (!ASSERT_OK_PTR(skel, "test_task_under_cgroup__open"))
		goto cleanup;

	skel->rodata->local_pid = getpid();
	skel->bss->remote_pid = getpid();
	skel->rodata->cgid = get_cgroup_id(FOO);

	ret = test_task_under_cgroup__load(skel);
	if (!ASSERT_OK(ret, "test_task_under_cgroup__load"))
		goto cleanup;

	ret = test_task_under_cgroup__attach(skel);
	if (!ASSERT_OK(ret, "test_task_under_cgroup__attach"))
		goto cleanup;

	pid = fork();
	if (pid == 0)
		exit(0);

	ret = (pid == -1);
	if (ASSERT_OK(ret, "fork process"))
		wait(NULL);

	test_task_under_cgroup__detach(skel);

	ASSERT_NEQ(skel->bss->remote_pid, skel->rodata->local_pid,
		   "test task_under_cgroup");

cleanup:
	test_task_under_cgroup__destroy(skel);
	close(foo);
}
