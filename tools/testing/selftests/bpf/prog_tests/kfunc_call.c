// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2021 Facebook */
#include <test_progs.h>
#include <network_helpers.h>
#include "kfunc_call_test.lskel.h"
#include "kfunc_call_test_subprog.skel.h"
#include "kfunc_call_test_subprog.lskel.h"

static void test_main(void)
{
	struct kfunc_call_test_lskel *skel;
	int prog_fd, err;
	LIBBPF_OPTS(bpf_test_run_opts, topts,
		.data_in = &pkt_v4,
		.data_size_in = sizeof(pkt_v4),
		.repeat = 1,
	);

	skel = kfunc_call_test_lskel__open_and_load();
	if (!ASSERT_OK_PTR(skel, "skel"))
		return;

	prog_fd = skel->progs.kfunc_call_test1.prog_fd;
	err = bpf_prog_test_run_opts(prog_fd, &topts);
	ASSERT_OK(err, "bpf_prog_test_run(test1)");
	ASSERT_EQ(topts.retval, 12, "test1-retval");

	prog_fd = skel->progs.kfunc_call_test2.prog_fd;
	err = bpf_prog_test_run_opts(prog_fd, &topts);
	ASSERT_OK(err, "bpf_prog_test_run(test2)");
	ASSERT_EQ(topts.retval, 3, "test2-retval");

	prog_fd = skel->progs.kfunc_call_test_ref_btf_id.prog_fd;
	err = bpf_prog_test_run_opts(prog_fd, &topts);
	ASSERT_OK(err, "bpf_prog_test_run(test_ref_btf_id)");
	ASSERT_EQ(topts.retval, 0, "test_ref_btf_id-retval");

	kfunc_call_test_lskel__destroy(skel);
}

static void test_subprog(void)
{
	struct kfunc_call_test_subprog *skel;
	int prog_fd, err;
	LIBBPF_OPTS(bpf_test_run_opts, topts,
		.data_in = &pkt_v4,
		.data_size_in = sizeof(pkt_v4),
		.repeat = 1,
	);

	skel = kfunc_call_test_subprog__open_and_load();
	if (!ASSERT_OK_PTR(skel, "skel"))
		return;

	prog_fd = bpf_program__fd(skel->progs.kfunc_call_test1);
	err = bpf_prog_test_run_opts(prog_fd, &topts);
	ASSERT_OK(err, "bpf_prog_test_run(test1)");
	ASSERT_EQ(topts.retval, 10, "test1-retval");
	ASSERT_NEQ(skel->data->active_res, -1, "active_res");
	ASSERT_EQ(skel->data->sk_state_res, BPF_TCP_CLOSE, "sk_state_res");

	kfunc_call_test_subprog__destroy(skel);
}

static void test_subprog_lskel(void)
{
	struct kfunc_call_test_subprog_lskel *skel;
	int prog_fd, err;
	LIBBPF_OPTS(bpf_test_run_opts, topts,
		.data_in = &pkt_v4,
		.data_size_in = sizeof(pkt_v4),
		.repeat = 1,
	);

	skel = kfunc_call_test_subprog_lskel__open_and_load();
	if (!ASSERT_OK_PTR(skel, "skel"))
		return;

	prog_fd = skel->progs.kfunc_call_test1.prog_fd;
	err = bpf_prog_test_run_opts(prog_fd, &topts);
	ASSERT_OK(err, "bpf_prog_test_run(test1)");
	ASSERT_EQ(topts.retval, 10, "test1-retval");
	ASSERT_NEQ(skel->data->active_res, -1, "active_res");
	ASSERT_EQ(skel->data->sk_state_res, BPF_TCP_CLOSE, "sk_state_res");

	kfunc_call_test_subprog_lskel__destroy(skel);
}

void test_kfunc_call(void)
{
	if (test__start_subtest("main"))
		test_main();

	if (test__start_subtest("subprog"))
		test_subprog();

	if (test__start_subtest("subprog_lskel"))
		test_subprog_lskel();
}
