// SPDX-License-Identifier: GPL-2.0
/* Copyright (C) 2021. Huawei Technologies Co., Ltd */
#include <test_progs.h>
#include "dummy_st_ops.skel.h"

/* Need to keep consistent with definition in include/linux/bpf.h */
struct bpf_dummy_ops_state {
	int val;
};

static void test_dummy_st_ops_attach(void)
{
	struct dummy_st_ops *skel;
	struct bpf_link *link;

	skel = dummy_st_ops__open_and_load();
	if (!ASSERT_OK_PTR(skel, "dummy_st_ops_load"))
		return;

	link = bpf_map__attach_struct_ops(skel->maps.dummy_1);
	ASSERT_EQ(libbpf_get_error(link), -EOPNOTSUPP, "dummy_st_ops_attach");

	dummy_st_ops__destroy(skel);
}

static void test_dummy_init_ret_value(void)
{
	__u64 args[1] = {0};
	struct bpf_prog_test_run_attr attr = {
		.ctx_size_in = sizeof(args),
		.ctx_in = args,
	};
	struct dummy_st_ops *skel;
	int fd, err;

	skel = dummy_st_ops__open_and_load();
	if (!ASSERT_OK_PTR(skel, "dummy_st_ops_load"))
		return;

	fd = bpf_program__fd(skel->progs.test_1);
	attr.prog_fd = fd;
	err = bpf_prog_test_run_xattr(&attr);
	ASSERT_OK(err, "test_run");
	ASSERT_EQ(attr.retval, 0xf2f3f4f5, "test_ret");

	dummy_st_ops__destroy(skel);
}

static void test_dummy_init_ptr_arg(void)
{
	int exp_retval = 0xbeef;
	struct bpf_dummy_ops_state in_state = {
		.val = exp_retval,
	};
	__u64 args[1] = {(unsigned long)&in_state};
	struct bpf_prog_test_run_attr attr = {
		.ctx_size_in = sizeof(args),
		.ctx_in = args,
	};
	struct dummy_st_ops *skel;
	int fd, err;

	skel = dummy_st_ops__open_and_load();
	if (!ASSERT_OK_PTR(skel, "dummy_st_ops_load"))
		return;

	fd = bpf_program__fd(skel->progs.test_1);
	attr.prog_fd = fd;
	err = bpf_prog_test_run_xattr(&attr);
	ASSERT_OK(err, "test_run");
	ASSERT_EQ(in_state.val, 0x5a, "test_ptr_ret");
	ASSERT_EQ(attr.retval, exp_retval, "test_ret");

	dummy_st_ops__destroy(skel);
}

static void test_dummy_multiple_args(void)
{
	__u64 args[5] = {0, -100, 0x8a5f, 'c', 0x1234567887654321ULL};
	struct bpf_prog_test_run_attr attr = {
		.ctx_size_in = sizeof(args),
		.ctx_in = args,
	};
	struct dummy_st_ops *skel;
	int fd, err;
	size_t i;
	char name[8];

	skel = dummy_st_ops__open_and_load();
	if (!ASSERT_OK_PTR(skel, "dummy_st_ops_load"))
		return;

	fd = bpf_program__fd(skel->progs.test_2);
	attr.prog_fd = fd;
	err = bpf_prog_test_run_xattr(&attr);
	ASSERT_OK(err, "test_run");
	for (i = 0; i < ARRAY_SIZE(args); i++) {
		snprintf(name, sizeof(name), "arg %zu", i);
		ASSERT_EQ(skel->bss->test_2_args[i], args[i], name);
	}

	dummy_st_ops__destroy(skel);
}

void test_dummy_st_ops(void)
{
	if (test__start_subtest("dummy_st_ops_attach"))
		test_dummy_st_ops_attach();
	if (test__start_subtest("dummy_init_ret_value"))
		test_dummy_init_ret_value();
	if (test__start_subtest("dummy_init_ptr_arg"))
		test_dummy_init_ptr_arg();
	if (test__start_subtest("dummy_multiple_args"))
		test_dummy_multiple_args();
}
