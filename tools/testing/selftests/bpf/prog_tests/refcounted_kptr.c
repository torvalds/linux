// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2023 Meta Platforms, Inc. and affiliates. */

#include <test_progs.h>
#include <network_helpers.h>

#include "refcounted_kptr.skel.h"
#include "refcounted_kptr_fail.skel.h"

void test_refcounted_kptr(void)
{
	RUN_TESTS(refcounted_kptr);
}

void test_refcounted_kptr_fail(void)
{
	RUN_TESTS(refcounted_kptr_fail);
}

void test_refcounted_kptr_wrong_owner(void)
{
	LIBBPF_OPTS(bpf_test_run_opts, opts,
		    .data_in = &pkt_v4,
		    .data_size_in = sizeof(pkt_v4),
		    .repeat = 1,
	);
	struct refcounted_kptr *skel;
	int ret;

	skel = refcounted_kptr__open_and_load();
	if (!ASSERT_OK_PTR(skel, "refcounted_kptr__open_and_load"))
		return;

	ret = bpf_prog_test_run_opts(bpf_program__fd(skel->progs.rbtree_wrong_owner_remove_fail_a1), &opts);
	ASSERT_OK(ret, "rbtree_wrong_owner_remove_fail_a1");
	ASSERT_OK(opts.retval, "rbtree_wrong_owner_remove_fail_a1 retval");

	ret = bpf_prog_test_run_opts(bpf_program__fd(skel->progs.rbtree_wrong_owner_remove_fail_b), &opts);
	ASSERT_OK(ret, "rbtree_wrong_owner_remove_fail_b");
	ASSERT_OK(opts.retval, "rbtree_wrong_owner_remove_fail_b retval");

	ret = bpf_prog_test_run_opts(bpf_program__fd(skel->progs.rbtree_wrong_owner_remove_fail_a2), &opts);
	ASSERT_OK(ret, "rbtree_wrong_owner_remove_fail_a2");
	ASSERT_OK(opts.retval, "rbtree_wrong_owner_remove_fail_a2 retval");
	refcounted_kptr__destroy(skel);
}
