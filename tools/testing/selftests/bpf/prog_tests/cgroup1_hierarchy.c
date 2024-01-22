// SPDX-License-Identifier: GPL-2.0
/* Copyright (C) 2023 Yafang Shao <laoar.shao@gmail.com> */

#include <sys/types.h>
#include <unistd.h>
#include <test_progs.h>
#include "cgroup_helpers.h"
#include "test_cgroup1_hierarchy.skel.h"

static void bpf_cgroup1(struct test_cgroup1_hierarchy *skel)
{
	struct bpf_link *lsm_link, *fentry_link;
	int err;

	/* Attach LSM prog first */
	lsm_link = bpf_program__attach_lsm(skel->progs.lsm_run);
	if (!ASSERT_OK_PTR(lsm_link, "lsm_attach"))
		return;

	/* LSM prog will be triggered when attaching fentry */
	fentry_link = bpf_program__attach_trace(skel->progs.fentry_run);
	ASSERT_NULL(fentry_link, "fentry_attach_fail");

	err = bpf_link__destroy(lsm_link);
	ASSERT_OK(err, "destroy_lsm");
}

static void bpf_cgroup1_sleepable(struct test_cgroup1_hierarchy *skel)
{
	struct bpf_link *lsm_link, *fentry_link;
	int err;

	/* Attach LSM prog first */
	lsm_link = bpf_program__attach_lsm(skel->progs.lsm_s_run);
	if (!ASSERT_OK_PTR(lsm_link, "lsm_attach"))
		return;

	/* LSM prog will be triggered when attaching fentry */
	fentry_link = bpf_program__attach_trace(skel->progs.fentry_run);
	ASSERT_NULL(fentry_link, "fentry_attach_fail");

	err = bpf_link__destroy(lsm_link);
	ASSERT_OK(err, "destroy_lsm");
}

static void bpf_cgroup1_invalid_id(struct test_cgroup1_hierarchy *skel)
{
	struct bpf_link *lsm_link, *fentry_link;
	int err;

	/* Attach LSM prog first */
	lsm_link = bpf_program__attach_lsm(skel->progs.lsm_run);
	if (!ASSERT_OK_PTR(lsm_link, "lsm_attach"))
		return;

	/* LSM prog will be triggered when attaching fentry */
	fentry_link = bpf_program__attach_trace(skel->progs.fentry_run);
	if (!ASSERT_OK_PTR(fentry_link, "fentry_attach_success"))
		goto cleanup;

	err = bpf_link__destroy(fentry_link);
	ASSERT_OK(err, "destroy_lsm");

cleanup:
	err = bpf_link__destroy(lsm_link);
	ASSERT_OK(err, "destroy_fentry");
}

void test_cgroup1_hierarchy(void)
{
	struct test_cgroup1_hierarchy *skel;
	__u64 current_cgid;
	int hid, err;

	skel = test_cgroup1_hierarchy__open();
	if (!ASSERT_OK_PTR(skel, "open"))
		return;

	skel->bss->target_pid = getpid();

	err = bpf_program__set_attach_target(skel->progs.fentry_run, 0, "bpf_fentry_test1");
	if (!ASSERT_OK(err, "fentry_set_target"))
		goto destroy;

	err = test_cgroup1_hierarchy__load(skel);
	if (!ASSERT_OK(err, "load"))
		goto destroy;

	/* Setup cgroup1 hierarchy */
	err = setup_classid_environment();
	if (!ASSERT_OK(err, "setup_classid_environment"))
		goto destroy;

	err = join_classid();
	if (!ASSERT_OK(err, "join_cgroup1"))
		goto cleanup;

	current_cgid = get_classid_cgroup_id();
	if (!ASSERT_GE(current_cgid, 0, "cgroup1 id"))
		goto cleanup;

	hid = get_cgroup1_hierarchy_id("net_cls");
	if (!ASSERT_GE(hid, 0, "cgroup1 id"))
		goto cleanup;
	skel->bss->target_hid = hid;

	if (test__start_subtest("test_cgroup1_hierarchy")) {
		skel->bss->target_ancestor_cgid = current_cgid;
		bpf_cgroup1(skel);
	}

	if (test__start_subtest("test_root_cgid")) {
		skel->bss->target_ancestor_cgid = 1;
		skel->bss->target_ancestor_level = 0;
		bpf_cgroup1(skel);
	}

	if (test__start_subtest("test_invalid_level")) {
		skel->bss->target_ancestor_cgid = 1;
		skel->bss->target_ancestor_level = 1;
		bpf_cgroup1_invalid_id(skel);
	}

	if (test__start_subtest("test_invalid_cgid")) {
		skel->bss->target_ancestor_cgid = 0;
		bpf_cgroup1_invalid_id(skel);
	}

	if (test__start_subtest("test_invalid_hid")) {
		skel->bss->target_ancestor_cgid = 1;
		skel->bss->target_ancestor_level = 0;
		skel->bss->target_hid = -1;
		bpf_cgroup1_invalid_id(skel);
	}

	if (test__start_subtest("test_invalid_cgrp_name")) {
		skel->bss->target_hid = get_cgroup1_hierarchy_id("net_cl");
		skel->bss->target_ancestor_cgid = current_cgid;
		bpf_cgroup1_invalid_id(skel);
	}

	if (test__start_subtest("test_invalid_cgrp_name2")) {
		skel->bss->target_hid = get_cgroup1_hierarchy_id("net_cls,");
		skel->bss->target_ancestor_cgid = current_cgid;
		bpf_cgroup1_invalid_id(skel);
	}

	if (test__start_subtest("test_sleepable_prog")) {
		skel->bss->target_hid = hid;
		skel->bss->target_ancestor_cgid = current_cgid;
		bpf_cgroup1_sleepable(skel);
	}

cleanup:
	cleanup_classid_environment();
destroy:
	test_cgroup1_hierarchy__destroy(skel);
}
