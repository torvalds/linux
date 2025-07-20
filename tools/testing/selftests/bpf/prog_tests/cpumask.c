// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2023 Meta Platforms, Inc. and affiliates. */

#include <test_progs.h>
#include "cpumask_failure.skel.h"
#include "cpumask_success.skel.h"

static const char * const cpumask_success_testcases[] = {
	"test_alloc_free_cpumask",
	"test_set_clear_cpu",
	"test_setall_clear_cpu",
	"test_first_firstzero_cpu",
	"test_firstand_nocpu",
	"test_test_and_set_clear",
	"test_and_or_xor",
	"test_intersects_subset",
	"test_copy_any_anyand",
	"test_insert_leave",
	"test_insert_remove_release",
	"test_global_mask_rcu",
	"test_global_mask_array_one_rcu",
	"test_global_mask_array_rcu",
	"test_global_mask_array_l2_rcu",
	"test_global_mask_nested_rcu",
	"test_global_mask_nested_deep_rcu",
	"test_global_mask_nested_deep_array_rcu",
	"test_cpumask_weight",
	"test_refcount_null_tracking",
	"test_populate_reject_small_mask",
	"test_populate_reject_unaligned",
	"test_populate",
};

static void verify_success(const char *prog_name)
{
	struct cpumask_success *skel;
	struct bpf_program *prog;
	struct bpf_link *link = NULL;
	pid_t child_pid;
	int status, err;

	skel = cpumask_success__open();
	if (!ASSERT_OK_PTR(skel, "cpumask_success__open"))
		return;

	skel->bss->pid = getpid();
	skel->bss->nr_cpus = libbpf_num_possible_cpus();

	err = cpumask_success__load(skel);
	if (!ASSERT_OK(err, "cpumask_success__load"))
		goto cleanup;

	prog = bpf_object__find_program_by_name(skel->obj, prog_name);
	if (!ASSERT_OK_PTR(prog, "bpf_object__find_program_by_name"))
		goto cleanup;

	link = bpf_program__attach(prog);
	if (!ASSERT_OK_PTR(link, "bpf_program__attach"))
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
	cpumask_success__destroy(skel);
}

void test_cpumask(void)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(cpumask_success_testcases); i++) {
		if (!test__start_subtest(cpumask_success_testcases[i]))
			continue;

		verify_success(cpumask_success_testcases[i]);
	}

	RUN_TESTS(cpumask_failure);
}
