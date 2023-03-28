// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2022 Meta Platforms, Inc. and affiliates. */

#define _GNU_SOURCE
#include <cgroup_helpers.h>
#include <test_progs.h>

#include "cgrp_kfunc_failure.skel.h"
#include "cgrp_kfunc_success.skel.h"

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

	RUN_TESTS(cgrp_kfunc_failure);

cleanup:
	cleanup_cgroup_environment();
}
