// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2022 Meta Platforms, Inc. and affiliates.*/

#define _GNU_SOURCE
#include <unistd.h>
#include <sys/syscall.h>
#include <sys/types.h>
#include <test_progs.h>
#include <bpf/btf.h>
#include "rcu_read_lock.skel.h"
#include "cgroup_helpers.h"

static unsigned long long cgroup_id;

static void test_success(void)
{
	struct rcu_read_lock *skel;
	int err;

	skel = rcu_read_lock__open();
	if (!ASSERT_OK_PTR(skel, "skel_open"))
		return;

	skel->bss->target_pid = syscall(SYS_gettid);

	bpf_program__set_autoload(skel->progs.get_cgroup_id, true);
	bpf_program__set_autoload(skel->progs.task_succ, true);
	bpf_program__set_autoload(skel->progs.two_regions, true);
	bpf_program__set_autoload(skel->progs.non_sleepable_1, true);
	bpf_program__set_autoload(skel->progs.non_sleepable_2, true);
	bpf_program__set_autoload(skel->progs.task_trusted_non_rcuptr, true);
	bpf_program__set_autoload(skel->progs.rcu_read_lock_subprog, true);
	bpf_program__set_autoload(skel->progs.rcu_read_lock_global_subprog, true);
	bpf_program__set_autoload(skel->progs.rcu_read_lock_subprog_lock, true);
	bpf_program__set_autoload(skel->progs.rcu_read_lock_subprog_unlock, true);
	err = rcu_read_lock__load(skel);
	if (!ASSERT_OK(err, "skel_load"))
		goto out;

	err = rcu_read_lock__attach(skel);
	if (!ASSERT_OK(err, "skel_attach"))
		goto out;

	syscall(SYS_getpgid);

	ASSERT_EQ(skel->bss->task_storage_val, 2, "task_storage_val");
	ASSERT_EQ(skel->bss->cgroup_id, cgroup_id, "cgroup_id");
out:
	rcu_read_lock__destroy(skel);
}

static void test_rcuptr_acquire(void)
{
	struct rcu_read_lock *skel;
	int err;

	skel = rcu_read_lock__open();
	if (!ASSERT_OK_PTR(skel, "skel_open"))
		return;

	skel->bss->target_pid = syscall(SYS_gettid);

	bpf_program__set_autoload(skel->progs.task_acquire, true);
	err = rcu_read_lock__load(skel);
	if (!ASSERT_OK(err, "skel_load"))
		goto out;

	err = rcu_read_lock__attach(skel);
	ASSERT_OK(err, "skel_attach");
out:
	rcu_read_lock__destroy(skel);
}

static const char * const inproper_region_tests[] = {
	"miss_lock",
	"no_lock",
	"miss_unlock",
	"non_sleepable_rcu_mismatch",
	"inproper_sleepable_helper",
	"inproper_sleepable_kfunc",
	"nested_rcu_region",
	"rcu_read_lock_global_subprog_lock",
	"rcu_read_lock_global_subprog_unlock",
};

static void test_inproper_region(void)
{
	struct rcu_read_lock *skel;
	struct bpf_program *prog;
	int i, err;

	for (i = 0; i < ARRAY_SIZE(inproper_region_tests); i++) {
		skel = rcu_read_lock__open();
		if (!ASSERT_OK_PTR(skel, "skel_open"))
			return;

		prog = bpf_object__find_program_by_name(skel->obj, inproper_region_tests[i]);
		if (!ASSERT_OK_PTR(prog, "bpf_object__find_program_by_name"))
			goto out;
		bpf_program__set_autoload(prog, true);
		err = rcu_read_lock__load(skel);
		ASSERT_ERR(err, "skel_load");
out:
		rcu_read_lock__destroy(skel);
	}
}

static const char * const rcuptr_misuse_tests[] = {
	"task_untrusted_rcuptr",
	"cross_rcu_region",
};

static void test_rcuptr_misuse(void)
{
	struct rcu_read_lock *skel;
	struct bpf_program *prog;
	int i, err;

	for (i = 0; i < ARRAY_SIZE(rcuptr_misuse_tests); i++) {
		skel = rcu_read_lock__open();
		if (!ASSERT_OK_PTR(skel, "skel_open"))
			return;

		prog = bpf_object__find_program_by_name(skel->obj, rcuptr_misuse_tests[i]);
		if (!ASSERT_OK_PTR(prog, "bpf_object__find_program_by_name"))
			goto out;
		bpf_program__set_autoload(prog, true);
		err = rcu_read_lock__load(skel);
		ASSERT_ERR(err, "skel_load");
out:
		rcu_read_lock__destroy(skel);
	}
}

void test_rcu_read_lock(void)
{
	int cgroup_fd;

	cgroup_fd = test__join_cgroup("/rcu_read_lock");
	if (!ASSERT_GE(cgroup_fd, 0, "join_cgroup /rcu_read_lock"))
		goto out;

	cgroup_id = get_cgroup_id("/rcu_read_lock");
	if (test__start_subtest("success"))
		test_success();
	if (test__start_subtest("rcuptr_acquire"))
		test_rcuptr_acquire();
	if (test__start_subtest("negative_tests_inproper_region"))
		test_inproper_region();
	if (test__start_subtest("negative_tests_rcuptr_misuse"))
		test_rcuptr_misuse();
	close(cgroup_fd);
out:;
}
