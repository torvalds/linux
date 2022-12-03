// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2022 Meta Platforms, Inc. and affiliates. */

#define _GNU_SOURCE
#include <sys/wait.h>
#include <test_progs.h>
#include <unistd.h>

#include "task_kfunc_failure.skel.h"
#include "task_kfunc_success.skel.h"

static size_t log_buf_sz = 1 << 20; /* 1 MB */
static char obj_log_buf[1048576];

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

static const char * const success_tests[] = {
	"test_task_acquire_release_argument",
	"test_task_acquire_release_current",
	"test_task_acquire_leave_in_map",
	"test_task_xchg_release",
	"test_task_get_release",
	"test_task_current_acquire_release",
	"test_task_from_pid_arg",
	"test_task_from_pid_current",
	"test_task_from_pid_invalid",
};

static struct {
	const char *prog_name;
	const char *expected_err_msg;
} failure_tests[] = {
	{"task_kfunc_acquire_untrusted", "R1 must be referenced or trusted"},
	{"task_kfunc_acquire_fp", "arg#0 pointer type STRUCT task_struct must point"},
	{"task_kfunc_acquire_unsafe_kretprobe", "reg type unsupported for arg#0 function"},
	{"task_kfunc_acquire_trusted_walked", "R1 must be referenced or trusted"},
	{"task_kfunc_acquire_null", "arg#0 pointer type STRUCT task_struct must point"},
	{"task_kfunc_acquire_unreleased", "Unreleased reference"},
	{"task_kfunc_get_non_kptr_param", "arg#0 expected pointer to map value"},
	{"task_kfunc_get_non_kptr_acquired", "arg#0 expected pointer to map value"},
	{"task_kfunc_get_null", "arg#0 expected pointer to map value"},
	{"task_kfunc_xchg_unreleased", "Unreleased reference"},
	{"task_kfunc_get_unreleased", "Unreleased reference"},
	{"task_kfunc_release_untrusted", "arg#0 is untrusted_ptr_or_null_ expected ptr_ or socket"},
	{"task_kfunc_release_fp", "arg#0 pointer type STRUCT task_struct must point"},
	{"task_kfunc_release_null", "arg#0 is ptr_or_null_ expected ptr_ or socket"},
	{"task_kfunc_release_unacquired", "release kernel function bpf_task_release expects"},
	{"task_kfunc_from_pid_no_null_check", "arg#0 is ptr_or_null_ expected ptr_ or socket"},
	{"task_kfunc_from_lsm_task_free", "reg type unsupported for arg#0 function"},
};

static void verify_fail(const char *prog_name, const char *expected_err_msg)
{
	LIBBPF_OPTS(bpf_object_open_opts, opts);
	struct task_kfunc_failure *skel;
	int err, i;

	opts.kernel_log_buf = obj_log_buf;
	opts.kernel_log_size = log_buf_sz;
	opts.kernel_log_level = 1;

	skel = task_kfunc_failure__open_opts(&opts);
	if (!ASSERT_OK_PTR(skel, "task_kfunc_failure__open_opts"))
		goto cleanup;

	for (i = 0; i < ARRAY_SIZE(failure_tests); i++) {
		struct bpf_program *prog;
		const char *curr_name = failure_tests[i].prog_name;

		prog = bpf_object__find_program_by_name(skel->obj, curr_name);
		if (!ASSERT_OK_PTR(prog, "bpf_object__find_program_by_name"))
			goto cleanup;

		bpf_program__set_autoload(prog, !strcmp(curr_name, prog_name));
	}

	err = task_kfunc_failure__load(skel);
	if (!ASSERT_ERR(err, "unexpected load success"))
		goto cleanup;

	if (!ASSERT_OK_PTR(strstr(obj_log_buf, expected_err_msg), "expected_err_msg")) {
		fprintf(stderr, "Expected err_msg: %s\n", expected_err_msg);
		fprintf(stderr, "Verifier output: %s\n", obj_log_buf);
	}

cleanup:
	task_kfunc_failure__destroy(skel);
}

void test_task_kfunc(void)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(success_tests); i++) {
		if (!test__start_subtest(success_tests[i]))
			continue;

		run_success_test(success_tests[i]);
	}

	for (i = 0; i < ARRAY_SIZE(failure_tests); i++) {
		if (!test__start_subtest(failure_tests[i].prog_name))
			continue;

		verify_fail(failure_tests[i].prog_name, failure_tests[i].expected_err_msg);
	}
}
