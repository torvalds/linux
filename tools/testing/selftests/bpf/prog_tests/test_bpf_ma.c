// SPDX-License-Identifier: GPL-2.0
/* Copyright (C) 2023. Huawei Technologies Co., Ltd */
#define _GNU_SOURCE
#include <sched.h>
#include <pthread.h>
#include <stdbool.h>
#include <bpf/btf.h>
#include <test_progs.h>

#include "test_bpf_ma.skel.h"

void test_test_bpf_ma(void)
{
	struct test_bpf_ma *skel;
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
