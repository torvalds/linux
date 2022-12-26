// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2022 Meta Platforms, Inc. and affiliates. */

#define _GNU_SOURCE
#include <cgroup_helpers.h>
#include <test_progs.h>

#include "cgrp_kfunc_failure.skel.h"
#include "cgrp_kfunc_success.skel.h"

static size_t log_buf_sz = 1 << 20; /* 1 MB */
static char obj_log_buf[1048576];

static struct cgrp_kfunc_success *open_load_cgrp_kfunc_skel(void)
{
	struct cgrp_kfunc_success *skel;
	int err;

	skel = cgrp_kfunc_success__open();
	if (!ASSERT_OK_PTR(skel, "skel_open"))
		return NULL;

	skel->bss->pid = getpid();

	err = cgrp_kfunc_success__load(skel);
	if (!ASSERT_OK(err, "skel_load"))
		goto cleanup;

	return skel;

cleanup:
	cgrp_kfunc_success__destroy(skel);
	return NULL;
}

static int mkdir_rm_test_dir(void)
{
	int fd;
	const char *cgrp_path = "cgrp_kfunc";

	fd = create_and_get_cgroup(cgrp_path);
	if (!ASSERT_GT(fd, 0, "mkdir_cgrp_fd"))
		return -1;

	close(fd);
	remove_cgroup(cgrp_path);

	return 0;
}

static void run_success_test(const char *prog_name)
{
	struct cgrp_kfunc_success *skel;
	struct bpf_program *prog;
	struct bpf_link *link = NULL;

	skel = open_load_cgrp_kfunc_skel();
	if (!ASSERT_OK_PTR(skel, "open_load_skel"))
		return;

	if (!ASSERT_OK(skel->bss->err, "pre_mkdir_err"))
		goto cleanup;

	prog = bpf_object__find_program_by_name(skel->obj, prog_name);
	if (!ASSERT_OK_PTR(prog, "bpf_object__find_program_by_name"))
		goto cleanup;

	link = bpf_program__attach(prog);
	if (!ASSERT_OK_PTR(link, "attached_link"))
		goto cleanup;

	ASSERT_EQ(skel->bss->invocations, 0, "pre_rmdir_count");
	if (!ASSERT_OK(mkdir_rm_test_dir(), "cgrp_mkdir"))
		goto cleanup;

	ASSERT_EQ(skel->bss->invocations, 1, "post_rmdir_count");
	ASSERT_OK(skel->bss->err, "post_rmdir_err");

cleanup:
	bpf_link__destroy(link);
	cgrp_kfunc_success__destroy(skel);
}

static const char * const success_tests[] = {
	"test_cgrp_acquire_release_argument",
	"test_cgrp_acquire_leave_in_map",
	"test_cgrp_xchg_release",
	"test_cgrp_get_release",
	"test_cgrp_get_ancestors",
};

static struct {
	const char *prog_name;
	const char *expected_err_msg;
} failure_tests[] = {
	{"cgrp_kfunc_acquire_untrusted", "R1 must be referenced or trusted"},
	{"cgrp_kfunc_acquire_fp", "arg#0 pointer type STRUCT cgroup must point"},
	{"cgrp_kfunc_acquire_unsafe_kretprobe", "reg type unsupported for arg#0 function"},
	{"cgrp_kfunc_acquire_trusted_walked", "R1 must be referenced or trusted"},
	{"cgrp_kfunc_acquire_null", "arg#0 pointer type STRUCT cgroup must point"},
	{"cgrp_kfunc_acquire_unreleased", "Unreleased reference"},
	{"cgrp_kfunc_get_non_kptr_param", "arg#0 expected pointer to map value"},
	{"cgrp_kfunc_get_non_kptr_acquired", "arg#0 expected pointer to map value"},
	{"cgrp_kfunc_get_null", "arg#0 expected pointer to map value"},
	{"cgrp_kfunc_xchg_unreleased", "Unreleased reference"},
	{"cgrp_kfunc_get_unreleased", "Unreleased reference"},
	{"cgrp_kfunc_release_untrusted", "arg#0 is untrusted_ptr_or_null_ expected ptr_ or socket"},
	{"cgrp_kfunc_release_fp", "arg#0 pointer type STRUCT cgroup must point"},
	{"cgrp_kfunc_release_null", "arg#0 is ptr_or_null_ expected ptr_ or socket"},
	{"cgrp_kfunc_release_unacquired", "release kernel function bpf_cgroup_release expects"},
};

static void verify_fail(const char *prog_name, const char *expected_err_msg)
{
	LIBBPF_OPTS(bpf_object_open_opts, opts);
	struct cgrp_kfunc_failure *skel;
	int err, i;

	opts.kernel_log_buf = obj_log_buf;
	opts.kernel_log_size = log_buf_sz;
	opts.kernel_log_level = 1;

	skel = cgrp_kfunc_failure__open_opts(&opts);
	if (!ASSERT_OK_PTR(skel, "cgrp_kfunc_failure__open_opts"))
		goto cleanup;

	for (i = 0; i < ARRAY_SIZE(failure_tests); i++) {
		struct bpf_program *prog;
		const char *curr_name = failure_tests[i].prog_name;

		prog = bpf_object__find_program_by_name(skel->obj, curr_name);
		if (!ASSERT_OK_PTR(prog, "bpf_object__find_program_by_name"))
			goto cleanup;

		bpf_program__set_autoload(prog, !strcmp(curr_name, prog_name));
	}

	err = cgrp_kfunc_failure__load(skel);
	if (!ASSERT_ERR(err, "unexpected load success"))
		goto cleanup;

	if (!ASSERT_OK_PTR(strstr(obj_log_buf, expected_err_msg), "expected_err_msg")) {
		fprintf(stderr, "Expected err_msg: %s\n", expected_err_msg);
		fprintf(stderr, "Verifier output: %s\n", obj_log_buf);
	}

cleanup:
	cgrp_kfunc_failure__destroy(skel);
}

void test_cgrp_kfunc(void)
{
	int i, err;

	err = setup_cgroup_environment();
	if (!ASSERT_OK(err, "cgrp_env_setup"))
		goto cleanup;

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

cleanup:
	cleanup_cgroup_environment();
}
