// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2023 Meta Platforms, Inc. and affiliates. */

#include <test_progs.h>
#include <network_helpers.h>

#include "local_kptr_stash.skel.h"
#include "local_kptr_stash_fail.skel.h"
static void test_local_kptr_stash_simple(void)
{
	LIBBPF_OPTS(bpf_test_run_opts, opts,
		    .data_in = &pkt_v4,
		    .data_size_in = sizeof(pkt_v4),
		    .repeat = 1,
	);
	struct local_kptr_stash *skel;
	int ret;

	skel = local_kptr_stash__open_and_load();
	if (!ASSERT_OK_PTR(skel, "local_kptr_stash__open_and_load"))
		return;

	ret = bpf_prog_test_run_opts(bpf_program__fd(skel->progs.stash_rb_nodes), &opts);
	ASSERT_OK(ret, "local_kptr_stash_add_nodes run");
	ASSERT_OK(opts.retval, "local_kptr_stash_add_nodes retval");

	local_kptr_stash__destroy(skel);
}

static void test_local_kptr_stash_plain(void)
{
	LIBBPF_OPTS(bpf_test_run_opts, opts,
		    .data_in = &pkt_v4,
		    .data_size_in = sizeof(pkt_v4),
		    .repeat = 1,
	);
	struct local_kptr_stash *skel;
	int ret;

	skel = local_kptr_stash__open_and_load();
	if (!ASSERT_OK_PTR(skel, "local_kptr_stash__open_and_load"))
		return;

	ret = bpf_prog_test_run_opts(bpf_program__fd(skel->progs.stash_plain), &opts);
	ASSERT_OK(ret, "local_kptr_stash_add_plain run");
	ASSERT_OK(opts.retval, "local_kptr_stash_add_plain retval");

	local_kptr_stash__destroy(skel);
}

static void test_local_kptr_stash_local_with_root(void)
{
	LIBBPF_OPTS(bpf_test_run_opts, opts,
		    .data_in = &pkt_v4,
		    .data_size_in = sizeof(pkt_v4),
		    .repeat = 1,
	);
	struct local_kptr_stash *skel;
	int ret;

	skel = local_kptr_stash__open_and_load();
	if (!ASSERT_OK_PTR(skel, "local_kptr_stash__open_and_load"))
		return;

	ret = bpf_prog_test_run_opts(bpf_program__fd(skel->progs.stash_local_with_root), &opts);
	ASSERT_OK(ret, "local_kptr_stash_add_local_with_root run");
	ASSERT_OK(opts.retval, "local_kptr_stash_add_local_with_root retval");

	local_kptr_stash__destroy(skel);
}

static void test_local_kptr_stash_unstash(void)
{
	LIBBPF_OPTS(bpf_test_run_opts, opts,
		    .data_in = &pkt_v4,
		    .data_size_in = sizeof(pkt_v4),
		    .repeat = 1,
	);
	struct local_kptr_stash *skel;
	int ret;

	skel = local_kptr_stash__open_and_load();
	if (!ASSERT_OK_PTR(skel, "local_kptr_stash__open_and_load"))
		return;

	ret = bpf_prog_test_run_opts(bpf_program__fd(skel->progs.stash_rb_nodes), &opts);
	ASSERT_OK(ret, "local_kptr_stash_add_nodes run");
	ASSERT_OK(opts.retval, "local_kptr_stash_add_nodes retval");

	ret = bpf_prog_test_run_opts(bpf_program__fd(skel->progs.unstash_rb_node), &opts);
	ASSERT_OK(ret, "local_kptr_stash_add_nodes run");
	ASSERT_EQ(opts.retval, 42, "local_kptr_stash_add_nodes retval");

	local_kptr_stash__destroy(skel);
}

static void test_refcount_acquire_without_unstash(void)
{
	LIBBPF_OPTS(bpf_test_run_opts, opts,
		    .data_in = &pkt_v4,
		    .data_size_in = sizeof(pkt_v4),
		    .repeat = 1,
	);
	struct local_kptr_stash *skel;
	int ret;

	skel = local_kptr_stash__open_and_load();
	if (!ASSERT_OK_PTR(skel, "local_kptr_stash__open_and_load"))
		return;

	ret = bpf_prog_test_run_opts(bpf_program__fd(skel->progs.refcount_acquire_without_unstash),
				     &opts);
	ASSERT_OK(ret, "refcount_acquire_without_unstash run");
	ASSERT_EQ(opts.retval, 2, "refcount_acquire_without_unstash retval");

	ret = bpf_prog_test_run_opts(bpf_program__fd(skel->progs.stash_refcounted_node), &opts);
	ASSERT_OK(ret, "stash_refcounted_node run");
	ASSERT_OK(opts.retval, "stash_refcounted_node retval");

	ret = bpf_prog_test_run_opts(bpf_program__fd(skel->progs.refcount_acquire_without_unstash),
				     &opts);
	ASSERT_OK(ret, "refcount_acquire_without_unstash (2) run");
	ASSERT_EQ(opts.retval, 42, "refcount_acquire_without_unstash (2) retval");

	local_kptr_stash__destroy(skel);
}

static void test_local_kptr_stash_fail(void)
{
	RUN_TESTS(local_kptr_stash_fail);
}

void test_local_kptr_stash(void)
{
	if (test__start_subtest("local_kptr_stash_simple"))
		test_local_kptr_stash_simple();
	if (test__start_subtest("local_kptr_stash_plain"))
		test_local_kptr_stash_plain();
	if (test__start_subtest("local_kptr_stash_local_with_root"))
		test_local_kptr_stash_local_with_root();
	if (test__start_subtest("local_kptr_stash_unstash"))
		test_local_kptr_stash_unstash();
	if (test__start_subtest("refcount_acquire_without_unstash"))
		test_refcount_acquire_without_unstash();
	if (test__start_subtest("local_kptr_stash_fail"))
		test_local_kptr_stash_fail();
}
