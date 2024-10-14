// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2023 Meta Platforms, Inc. and affiliates. */

#include <sys/syscall.h>
#include <sys/mman.h>
#include <sys/wait.h>
#include <unistd.h>
#include <malloc.h>
#include <stdlib.h>
#include <test_progs.h>
#include "cgroup_helpers.h"

#include "iters.skel.h"
#include "iters_state_safety.skel.h"
#include "iters_looping.skel.h"
#include "iters_num.skel.h"
#include "iters_testmod.skel.h"
#include "iters_testmod_seq.skel.h"
#include "iters_task_vma.skel.h"
#include "iters_task.skel.h"
#include "iters_css_task.skel.h"
#include "iters_css.skel.h"
#include "iters_task_failure.skel.h"

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

static pthread_mutex_t do_nothing_mutex;

static void *do_nothing_wait(void *arg)
{
	pthread_mutex_lock(&do_nothing_mutex);
	pthread_mutex_unlock(&do_nothing_mutex);

	pthread_exit(arg);
}

#define thread_num 2

static void subtest_task_iters(void)
{
	struct iters_task *skel = NULL;
	pthread_t thread_ids[thread_num];
	void *ret;
	int err;

	skel = iters_task__open_and_load();
	if (!ASSERT_OK_PTR(skel, "open_and_load"))
		goto cleanup;
	skel->bss->target_pid = getpid();
	err = iters_task__attach(skel);
	if (!ASSERT_OK(err, "iters_task__attach"))
		goto cleanup;
	pthread_mutex_lock(&do_nothing_mutex);
	for (int i = 0; i < thread_num; i++)
		ASSERT_OK(pthread_create(&thread_ids[i], NULL, &do_nothing_wait, NULL),
			"pthread_create");

	syscall(SYS_getpgid);
	iters_task__detach(skel);
	ASSERT_EQ(skel->bss->procs_cnt, 1, "procs_cnt");
	ASSERT_EQ(skel->bss->threads_cnt, thread_num + 1, "threads_cnt");
	ASSERT_EQ(skel->bss->proc_threads_cnt, thread_num + 1, "proc_threads_cnt");
	ASSERT_EQ(skel->bss->invalid_cnt, 0, "invalid_cnt");
	pthread_mutex_unlock(&do_nothing_mutex);
	for (int i = 0; i < thread_num; i++)
		ASSERT_OK(pthread_join(thread_ids[i], &ret), "pthread_join");
cleanup:
	iters_task__destroy(skel);
}

extern int stack_mprotect(void);

static void subtest_css_task_iters(void)
{
	struct iters_css_task *skel = NULL;
	int err, cg_fd, cg_id;
	const char *cgrp_path = "/cg1";

	err = setup_cgroup_environment();
	if (!ASSERT_OK(err, "setup_cgroup_environment"))
		goto cleanup;
	cg_fd = create_and_get_cgroup(cgrp_path);
	if (!ASSERT_GE(cg_fd, 0, "create_and_get_cgroup"))
		goto cleanup;
	cg_id = get_cgroup_id(cgrp_path);
	err = join_cgroup(cgrp_path);
	if (!ASSERT_OK(err, "join_cgroup"))
		goto cleanup;

	skel = iters_css_task__open_and_load();
	if (!ASSERT_OK_PTR(skel, "open_and_load"))
		goto cleanup;

	skel->bss->target_pid = getpid();
	skel->bss->cg_id = cg_id;
	err = iters_css_task__attach(skel);
	if (!ASSERT_OK(err, "iters_task__attach"))
		goto cleanup;
	err = stack_mprotect();
	if (!ASSERT_EQ(err, -1, "stack_mprotect") ||
	    !ASSERT_EQ(errno, EPERM, "stack_mprotect"))
		goto cleanup;
	iters_css_task__detach(skel);
	ASSERT_EQ(skel->bss->css_task_cnt, 1, "css_task_cnt");

cleanup:
	cleanup_cgroup_environment();
	iters_css_task__destroy(skel);
}

static void subtest_css_iters(void)
{
	struct iters_css *skel = NULL;
	struct {
		const char *path;
		int fd;
	} cgs[] = {
		{ "/cg1" },
		{ "/cg1/cg2" },
		{ "/cg1/cg2/cg3" },
		{ "/cg1/cg2/cg3/cg4" },
	};
	int err, cg_nr = ARRAY_SIZE(cgs);
	int i;

	err = setup_cgroup_environment();
	if (!ASSERT_OK(err, "setup_cgroup_environment"))
		goto cleanup;
	for (i = 0; i < cg_nr; i++) {
		cgs[i].fd = create_and_get_cgroup(cgs[i].path);
		if (!ASSERT_GE(cgs[i].fd, 0, "create_and_get_cgroup"))
			goto cleanup;
	}

	skel = iters_css__open_and_load();
	if (!ASSERT_OK_PTR(skel, "open_and_load"))
		goto cleanup;

	skel->bss->target_pid = getpid();
	skel->bss->root_cg_id = get_cgroup_id(cgs[0].path);
	skel->bss->leaf_cg_id = get_cgroup_id(cgs[cg_nr - 1].path);
	err = iters_css__attach(skel);

	if (!ASSERT_OK(err, "iters_task__attach"))
		goto cleanup;

	syscall(SYS_getpgid);
	ASSERT_EQ(skel->bss->pre_order_cnt, cg_nr, "pre_order_cnt");
	ASSERT_EQ(skel->bss->first_cg_id, get_cgroup_id(cgs[0].path), "first_cg_id");

	ASSERT_EQ(skel->bss->post_order_cnt, cg_nr, "post_order_cnt");
	ASSERT_EQ(skel->bss->last_cg_id, get_cgroup_id(cgs[0].path), "last_cg_id");
	ASSERT_EQ(skel->bss->tree_high, cg_nr - 1, "tree_high");
	iters_css__detach(skel);
cleanup:
	cleanup_cgroup_environment();
	iters_css__destroy(skel);
}

void test_iters(void)
{
	RUN_TESTS(iters_state_safety);
	RUN_TESTS(iters_looping);
	RUN_TESTS(iters);
	RUN_TESTS(iters_css_task);

	if (env.has_testmod) {
		RUN_TESTS(iters_testmod);
		RUN_TESTS(iters_testmod_seq);
	}

	if (test__start_subtest("num"))
		subtest_num_iters();
	if (test__start_subtest("testmod_seq"))
		subtest_testmod_seq_iters();
	if (test__start_subtest("task_vma"))
		subtest_task_vma_iters();
	if (test__start_subtest("task"))
		subtest_task_iters();
	if (test__start_subtest("css_task"))
		subtest_css_task_iters();
	if (test__start_subtest("css"))
		subtest_css_iters();
	RUN_TESTS(iters_task_failure);
}
