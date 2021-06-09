// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2021 Facebook */
#include <test_progs.h>
#include <network_helpers.h>
#include "kfunc_call_test.skel.h"
#include "kfunc_call_test_subprog.skel.h"

static void test_main(void)
{
	struct kfunc_call_test *skel;
	int prog_fd, retval, err;

	skel = kfunc_call_test__open_and_load();
	if (!ASSERT_OK_PTR(skel, "skel"))
		return;

	prog_fd = bpf_program__fd(skel->progs.kfunc_call_test1);
	err = bpf_prog_test_run(prog_fd, 1, &pkt_v4, sizeof(pkt_v4),
				NULL, NULL, (__u32 *)&retval, NULL);
	ASSERT_OK(err, "bpf_prog_test_run(test1)");
	ASSERT_EQ(retval, 12, "test1-retval");

	prog_fd = bpf_program__fd(skel->progs.kfunc_call_test2);
	err = bpf_prog_test_run(prog_fd, 1, &pkt_v4, sizeof(pkt_v4),
				NULL, NULL, (__u32 *)&retval, NULL);
	ASSERT_OK(err, "bpf_prog_test_run(test2)");
	ASSERT_EQ(retval, 3, "test2-retval");

	kfunc_call_test__destroy(skel);
}

static void test_subprog(void)
{
	struct kfunc_call_test_subprog *skel;
	int prog_fd, retval, err;

	skel = kfunc_call_test_subprog__open_and_load();
	if (!ASSERT_OK_PTR(skel, "skel"))
		return;

	prog_fd = bpf_program__fd(skel->progs.kfunc_call_test1);
	err = bpf_prog_test_run(prog_fd, 1, &pkt_v4, sizeof(pkt_v4),
				NULL, NULL, (__u32 *)&retval, NULL);
	ASSERT_OK(err, "bpf_prog_test_run(test1)");
	ASSERT_EQ(retval, 10, "test1-retval");
	ASSERT_NEQ(skel->data->active_res, -1, "active_res");
	ASSERT_EQ(skel->data->sk_state, BPF_TCP_CLOSE, "sk_state");

	kfunc_call_test_subprog__destroy(skel);
}

void test_kfunc_call(void)
{
	if (test__start_subtest("main"))
		test_main();

	if (test__start_subtest("subprog"))
		test_subprog();
}
