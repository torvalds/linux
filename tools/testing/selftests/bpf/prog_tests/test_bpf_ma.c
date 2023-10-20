// SPDX-License-Identifier: GPL-2.0
/* Copyright (C) 2023. Huawei Technologies Co., Ltd */
#define _GNU_SOURCE
#include <sched.h>
#include <pthread.h>
#include <stdbool.h>
#include <bpf/btf.h>
#include <test_progs.h>

#include "test_bpf_ma.skel.h"

static void do_bpf_ma_test(const char *name)
{
	struct test_bpf_ma *skel;
	struct bpf_program *prog;
	struct btf *btf;
	int i, err;

	skel = test_bpf_ma__open();
	if (!ASSERT_OK_PTR(skel, "open"))
		return;

	btf = bpf_object__btf(skel->obj);
	if (!ASSERT_OK_PTR(btf, "btf"))
		goto out;

	for (i = 0; i < ARRAY_SIZE(skel->rodata->data_sizes); i++) {
		char name[32];
		int id;

		snprintf(name, sizeof(name), "bin_data_%u", skel->rodata->data_sizes[i]);
		id = btf__find_by_name_kind(btf, name, BTF_KIND_STRUCT);
		if (!ASSERT_GT(id, 0, "bin_data"))
			goto out;
		skel->rodata->data_btf_ids[i] = id;
	}

	prog = bpf_object__find_program_by_name(skel->obj, name);
	if (!ASSERT_OK_PTR(prog, "invalid prog name"))
		goto out;
	bpf_program__set_autoload(prog, true);

	err = test_bpf_ma__load(skel);
	if (!ASSERT_OK(err, "load"))
		goto out;

	err = test_bpf_ma__attach(skel);
	if (!ASSERT_OK(err, "attach"))
		goto out;

	skel->bss->pid = getpid();
	usleep(1);
	ASSERT_OK(skel->bss->err, "test error");
out:
	test_bpf_ma__destroy(skel);
}

void test_test_bpf_ma(void)
{
	if (test__start_subtest("batch_alloc_free"))
		do_bpf_ma_test("test_batch_alloc_free");
	if (test__start_subtest("free_through_map_free"))
		do_bpf_ma_test("test_free_through_map_free");
	if (test__start_subtest("batch_percpu_alloc_free"))
		do_bpf_ma_test("test_batch_percpu_alloc_free");
	if (test__start_subtest("percpu_free_through_map_free"))
		do_bpf_ma_test("test_percpu_free_through_map_free");
}
