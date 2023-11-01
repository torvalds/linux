// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2023 Meta Platforms, Inc. and affiliates. */

#include <test_progs.h>

#include "iters.skel.h"
#include "iters_state_safety.skel.h"
#include "iters_looping.skel.h"
#include "iters_num.skel.h"
#include "iters_testmod_seq.skel.h"
#include "iters_task_vma.skel.h"

static void subtest_num_iters(void)
{
	struct iters_num *skel;
	int err;

	skel = iters_num__open_and_load();
	if (!ASSERT_OK_PTR(skel, "skel_open_and_load"))
		return;

	err = iters_num__attach(skel);
	if (!ASSERT_OK(err, "skel_attach"))
		goto cleanup;

	usleep(1);
	iters_num__detach(skel);

#define VALIDATE_CASE(case_name)					\
	ASSERT_EQ(skel->bss->res_##case_name,				\
		  skel->rodata->exp_##case_name,			\
		  #case_name)

	VALIDATE_CASE(empty_zero);
	VALIDATE_CASE(empty_int_min);
	VALIDATE_CASE(empty_int_max);
	VALIDATE_CASE(empty_minus_one);

	VALIDATE_CASE(simple_sum);
	VALIDATE_CASE(neg_sum);
	VALIDATE_CASE(very_neg_sum);
	VALIDATE_CASE(neg_pos_sum);

	VALIDATE_CASE(invalid_range);
	VALIDATE_CASE(max_range);
	VALIDATE_CASE(e2big_range);

	VALIDATE_CASE(succ_elem_cnt);
	VALIDATE_CASE(overfetched_elem_cnt);
	VALIDATE_CASE(fail_elem_cnt);

#undef VALIDATE_CASE

cleanup:
	iters_num__destroy(skel);
}

static void subtest_testmod_seq_iters(void)
{
	struct iters_testmod_seq *skel;
	int err;

	if (!env.has_testmod) {
		test__skip();
		return;
	}

	skel = iters_testmod_seq__open_and_load();
	if (!ASSERT_OK_PTR(skel, "skel_open_and_load"))
		return;

	err = iters_testmod_seq__attach(skel);
	if (!ASSERT_OK(err, "skel_attach"))
		goto cleanup;

	usleep(1);
	iters_testmod_seq__detach(skel);

#define VALIDATE_CASE(case_name)					\
	ASSERT_EQ(skel->bss->res_##case_name,				\
		  skel->rodata->exp_##case_name,			\
		  #case_name)

	VALIDATE_CASE(empty);
	VALIDATE_CASE(full);
	VALIDATE_CASE(truncated);

#undef VALIDATE_CASE

cleanup:
	iters_testmod_seq__destroy(skel);
}

static void subtest_task_vma_iters(void)
{
	unsigned long start, end, bpf_iter_start, bpf_iter_end;
	struct iters_task_vma *skel;
	char rest_of_line[1000];
	unsigned int seen;
	FILE *f = NULL;
	int err;

	skel = iters_task_vma__open_and_load();
	if (!ASSERT_OK_PTR(skel, "skel_open_and_load"))
		return;

	skel->bss->target_pid = getpid();

	err = iters_task_vma__attach(skel);
	if (!ASSERT_OK(err, "skel_attach"))
		goto cleanup;

	getpgid(skel->bss->target_pid);
	iters_task_vma__detach(skel);

	if (!ASSERT_GT(skel->bss->vmas_seen, 0, "vmas_seen_gt_zero"))
		goto cleanup;

	f = fopen("/proc/self/maps", "r");
	if (!ASSERT_OK_PTR(f, "proc_maps_fopen"))
		goto cleanup;

	seen = 0;
	while (fscanf(f, "%lx-%lx %[^\n]\n", &start, &end, rest_of_line) == 3) {
		/* [vsyscall] vma isn't _really_ part of task->mm vmas.
		 * /proc/PID/maps returns it when out of vmas - see get_gate_vma
		 * calls in fs/proc/task_mmu.c
		 */
		if (strstr(rest_of_line, "[vsyscall]"))
			continue;

		bpf_iter_start = skel->bss->vm_ranges[seen].vm_start;
		bpf_iter_end = skel->bss->vm_ranges[seen].vm_end;

		ASSERT_EQ(bpf_iter_start, start, "vma->vm_start match");
		ASSERT_EQ(bpf_iter_end, end, "vma->vm_end match");
		seen++;
	}

	if (!ASSERT_EQ(skel->bss->vmas_seen, seen, "vmas_seen_eq"))
		goto cleanup;

cleanup:
	if (f)
		fclose(f);
	iters_task_vma__destroy(skel);
}

void test_iters(void)
{
	RUN_TESTS(iters_state_safety);
	RUN_TESTS(iters_looping);
	RUN_TESTS(iters);

	if (env.has_testmod)
		RUN_TESTS(iters_testmod_seq);

	if (test__start_subtest("num"))
		subtest_num_iters();
	if (test__start_subtest("testmod_seq"))
		subtest_testmod_seq_iters();
	if (test__start_subtest("task_vma"))
		subtest_task_vma_iters();
}
