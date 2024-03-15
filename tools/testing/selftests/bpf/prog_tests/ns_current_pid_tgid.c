// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2020 Carlos Neira cneirabustos@gmail.com */

#define _GNU_SOURCE
#include <test_progs.h>
#include "test_ns_current_pid_tgid.skel.h"
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include <sys/syscall.h>
#include <sched.h>
#include <sys/wait.h>
#include <sys/mount.h>
#include <sys/fcntl.h>

#define STACK_SIZE (1024 * 1024)
static char child_stack[STACK_SIZE];

static int test_current_pid_tgid(void *args)
{
	struct test_ns_current_pid_tgid__bss  *bss;
	struct test_ns_current_pid_tgid *skel;
	int ret = -1, err;
	pid_t tgid, pid;
	struct stat st;

	skel = test_ns_current_pid_tgid__open_and_load();
	if (!ASSERT_OK_PTR(skel, "test_ns_current_pid_tgid__open_and_load"))
		goto out;

	pid = syscall(SYS_gettid);
	tgid = getpid();

	err = stat("/proc/self/ns/pid", &st);
	if (!ASSERT_OK(err, "stat /proc/self/ns/pid"))
		goto cleanup;

	bss = skel->bss;
	bss->dev = st.st_dev;
	bss->ino = st.st_ino;
	bss->user_pid = 0;
	bss->user_tgid = 0;

	err = test_ns_current_pid_tgid__attach(skel);
	if (!ASSERT_OK(err, "test_ns_current_pid_tgid__attach"))
		goto cleanup;

	/* trigger tracepoint */
	usleep(1);
	if (!ASSERT_EQ(bss->user_pid, pid, "pid"))
		goto cleanup;
	if (!ASSERT_EQ(bss->user_tgid, tgid, "tgid"))
		goto cleanup;
	ret = 0;

cleanup:
	test_ns_current_pid_tgid__destroy(skel);
out:
	return ret;
}

static void test_ns_current_pid_tgid_new_ns(void)
{
	int wstatus;
	pid_t cpid;

	/* Create a process in a new namespace, this process
	 * will be the init process of this new namespace hence will be pid 1.
	 */
	cpid = clone(test_current_pid_tgid, child_stack + STACK_SIZE,
		     CLONE_NEWPID | SIGCHLD, NULL);

	if (!ASSERT_NEQ(cpid, -1, "clone"))
		return;

	if (!ASSERT_NEQ(waitpid(cpid, &wstatus, 0), -1, "waitpid"))
		return;

	if (!ASSERT_OK(WEXITSTATUS(wstatus), "newns_pidtgid"))
		return;
}

/* TODO: use a different tracepoint */
void serial_test_ns_current_pid_tgid(void)
{
	if (test__start_subtest("root_ns_tp"))
		test_current_pid_tgid(NULL);
	if (test__start_subtest("new_ns_tp"))
		test_ns_current_pid_tgid_new_ns();
}
