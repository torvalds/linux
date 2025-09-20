// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2022 Meta Platforms, Inc. and affiliates. */

#define _GNU_SOURCE
#include <sys/wait.h>
#include <test_progs.h>
#include <unistd.h>

#include "task_kfunc_failure.skel.h"
#include "task_kfunc_success.skel.h"

static struct task_kfunc_success *open_load_task_kfunc_skel(void)
{
	struct task_kfunc_success *skel;
	int err;

	skel = task_kfunc_success__open();
	if (!ASSERT_OK_PTR(skel, "skel_open"))
		return NULL;

	skel->bss->pid = getpid();

	err = task_kfunc_success__load(skel);
	if (!ASSERT_OK(err, "skel_load"))
		goto cleanup;

	return skel;

cleanup:
	task_kfunc_success__destroy(skel);
	return NULL;
}

static void run_success_test(const char *prog_name)
{
	struct task_kfunc_success *skel;
	int status;
	pid_t child_pid;
	struct bpf_program *prog;
	struct bpf_link *link = NULL;

	skel = open_load_task_kfunc_skel();
	if (!ASSERT_OK_PTR(skel, "open_load_skel"))
		return;

	if (!ASSERT_OK(skel->bss->err, "pre_spawn_err"))
		goto cleanup;

	prog = bpf_object__find_program_by_name(skel->obj, prog_name);
	if (!ASSERT_OK_PTR(prog, "bpf_object__find_program_by_name"))
		goto cleanup;

	link = bpf_program__attach(prog);
	if (!ASSERT_OK_PTR(link, "attached_link"))
		goto cleanup;

	child_pid = fork();
	if (!ASSERT_GT(child_pid, -1, "child_pid"))
		goto cleanup;
	if (child_pid == 0)
		_exit(0);
	waitpid(child_pid, &status, 0);

	ASSERT_OK(skel->bss->err, "post_wait_err");

cleanup:
	bpf_link__destroy(link);
	task_kfunc_success__destroy(skel);
}

static int run_vpid_test(void *prog_name)
{
	struct task_kfunc_success *skel;
	struct bpf_program *prog;
	int prog_fd, err = 0;

	if (getpid() != 1)
		return 1;

	skel = open_load_task_kfunc_skel();
	if (!skel)
		return 2;

	if (skel->bss->err) {
		err = 3;
		goto cleanup;
	}

	prog = bpf_object__find_program_by_name(skel->obj, prog_name);
	if (!prog) {
		err = 4;
		goto cleanup;
	}

	prog_fd = bpf_program__fd(prog);
	if (prog_fd < 0) {
		err = 5;
		goto cleanup;
	}

	if (bpf_prog_test_run_opts(prog_fd, NULL)) {
		err = 6;
		goto cleanup;
	}

	if (skel->bss->err)
		err = 7 + skel->bss->err;
cleanup:
	task_kfunc_success__destroy(skel);
	return err;
}

static void run_vpid_success_test(const char *prog_name)
{
	const int stack_size = 1024 * 1024;
	int child_pid, wstatus;
	char *stack;

	stack = (char *)malloc(stack_size);
	if (!ASSERT_OK_PTR(stack, "clone_stack"))
		return;

	child_pid = clone(run_vpid_test, stack + stack_size,
			  CLONE_NEWPID | SIGCHLD, (void *)prog_name);
	if (!ASSERT_GT(child_pid, -1, "child_pid"))
		goto cleanup;

	if (!ASSERT_GT(waitpid(child_pid, &wstatus, 0), -1, "waitpid"))
		goto cleanup;

	if (WEXITSTATUS(wstatus) > 7)
		ASSERT_OK(WEXITSTATUS(wstatus) - 7, "vpid_test_failure");
	else
		ASSERT_OK(WEXITSTATUS(wstatus), "run_vpid_test_err");
cleanup:
	free(stack);
}

static const char * const success_tests[] = {
	"test_task_acquire_release_argument",
	"test_task_acquire_release_current",
	"test_task_acquire_leave_in_map",
	"test_task_xchg_release",
	"test_task_map_acquire_release",
	"test_task_current_acquire_release",
	"test_task_from_pid_arg",
	"test_task_from_pid_current",
	"test_task_from_pid_invalid",
	"task_kfunc_acquire_trusted_walked",
	"test_task_kfunc_flavor_relo",
	"test_task_kfunc_flavor_relo_not_found",
};

static const char * const vpid_success_tests[] = {
	"test_task_from_vpid_current",
	"test_task_from_vpid_invalid",
};

void test_task_kfunc(void)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(success_tests); i++) {
		if (!test__start_subtest(success_tests[i]))
			continue;

		run_success_test(success_tests[i]);
	}

	for (i = 0; i < ARRAY_SIZE(vpid_success_tests); i++) {
		if (!test__start_subtest(vpid_success_tests[i]))
			continue;

		run_vpid_success_test(vpid_success_tests[i]);
	}

	RUN_TESTS(task_kfunc_failure);
}
