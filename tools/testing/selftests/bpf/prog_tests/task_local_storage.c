// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2021 Facebook */

#define _GNU_SOURCE         /* See feature_test_macros(7) */
#include <unistd.h>
#include <sys/syscall.h>   /* For SYS_xxx definitions */
#include <sys/types.h>
#include <test_progs.h>
#include "task_local_storage.skel.h"
#include "task_local_storage_exit_creds.skel.h"
#include "task_ls_recursion.skel.h"

static void test_sys_enter_exit(void)
{
	struct task_local_storage *skel;
	int err;

	skel = task_local_storage__open_and_load();
	if (!ASSERT_OK_PTR(skel, "skel_open_and_load"))
		return;

	skel->bss->target_pid = syscall(SYS_gettid);

	err = task_local_storage__attach(skel);
	if (!ASSERT_OK(err, "skel_attach"))
		goto out;

	syscall(SYS_gettid);
	syscall(SYS_gettid);

	/* 3x syscalls: 1x attach and 2x gettid */
	ASSERT_EQ(skel->bss->enter_cnt, 3, "enter_cnt");
	ASSERT_EQ(skel->bss->exit_cnt, 3, "exit_cnt");
	ASSERT_EQ(skel->bss->mismatch_cnt, 0, "mismatch_cnt");
out:
	task_local_storage__destroy(skel);
}

static void test_exit_creds(void)
{
	struct task_local_storage_exit_creds *skel;
	int err;

	skel = task_local_storage_exit_creds__open_and_load();
	if (!ASSERT_OK_PTR(skel, "skel_open_and_load"))
		return;

	err = task_local_storage_exit_creds__attach(skel);
	if (!ASSERT_OK(err, "skel_attach"))
		goto out;

	/* trigger at least one exit_creds() */
	if (CHECK_FAIL(system("ls > /dev/null")))
		goto out;

	/* sync rcu to make sure exit_creds() is called for "ls" */
	kern_sync_rcu();
	ASSERT_EQ(skel->bss->valid_ptr_count, 0, "valid_ptr_count");
	ASSERT_NEQ(skel->bss->null_ptr_count, 0, "null_ptr_count");
out:
	task_local_storage_exit_creds__destroy(skel);
}

static void test_recursion(void)
{
	struct task_ls_recursion *skel;
	int err;

	skel = task_ls_recursion__open_and_load();
	if (!ASSERT_OK_PTR(skel, "skel_open_and_load"))
		return;

	err = task_ls_recursion__attach(skel);
	if (!ASSERT_OK(err, "skel_attach"))
		goto out;

	/* trigger sys_enter, make sure it does not cause deadlock */
	syscall(SYS_gettid);

out:
	task_ls_recursion__destroy(skel);
}

void test_task_local_storage(void)
{
	if (test__start_subtest("sys_enter_exit"))
		test_sys_enter_exit();
	if (test__start_subtest("exit_creds"))
		test_exit_creds();
	if (test__start_subtest("recursion"))
		test_recursion();
}
