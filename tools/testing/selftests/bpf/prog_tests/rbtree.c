// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2022 Meta Platforms, Inc. and affiliates. */

#include <test_progs.h>
#include <network_helpers.h>

#include "rbtree.skel.h"
#include "rbtree_fail.skel.h"
#include "rbtree_btf_fail__wrong_node_type.skel.h"
#include "rbtree_btf_fail__add_wrong_type.skel.h"

static void test_rbtree_add_nodes(void)
{
	LIBBPF_OPTS(bpf_test_run_opts, opts,
		    .data_in = &pkt_v4,
		    .data_size_in = sizeof(pkt_v4),
		    .repeat = 1,
	);
	struct rbtree *skel;
	int ret;

	skel = rbtree__open_and_load();
	if (!ASSERT_OK_PTR(skel, "rbtree__open_and_load"))
		return;

	ret = bpf_prog_test_run_opts(bpf_program__fd(skel->progs.rbtree_add_nodes), &opts);
	ASSERT_OK(ret, "rbtree_add_nodes run");
	ASSERT_OK(opts.retval, "rbtree_add_nodes retval");
	ASSERT_EQ(skel->data->less_callback_ran, 1, "rbtree_add_nodes less_callback_ran");

	rbtree__destroy(skel);
}

static void test_rbtree_add_nodes_nested(void)
{
	LIBBPF_OPTS(bpf_test_run_opts, opts,
		    .data_in = &pkt_v4,
		    .data_size_in = sizeof(pkt_v4),
		    .repeat = 1,
	);
	struct rbtree *skel;
	int ret;

	skel = rbtree__open_and_load();
	if (!ASSERT_OK_PTR(skel, "rbtree__open_and_load"))
		return;

	ret = bpf_prog_test_run_opts(bpf_program__fd(skel->progs.rbtree_add_nodes_nested), &opts);
	ASSERT_OK(ret, "rbtree_add_nodes_nested run");
	ASSERT_OK(opts.retval, "rbtree_add_nodes_nested retval");
	ASSERT_EQ(skel->data->less_callback_ran, 1, "rbtree_add_nodes_nested less_callback_ran");

	rbtree__destroy(skel);
}

static void test_rbtree_add_and_remove(void)
{
	LIBBPF_OPTS(bpf_test_run_opts, opts,
		    .data_in = &pkt_v4,
		    .data_size_in = sizeof(pkt_v4),
		    .repeat = 1,
	);
	struct rbtree *skel;
	int ret;

	skel = rbtree__open_and_load();
	if (!ASSERT_OK_PTR(skel, "rbtree__open_and_load"))
		return;

	ret = bpf_prog_test_run_opts(bpf_program__fd(skel->progs.rbtree_add_and_remove), &opts);
	ASSERT_OK(ret, "rbtree_add_and_remove");
	ASSERT_OK(opts.retval, "rbtree_add_and_remove retval");
	ASSERT_EQ(skel->data->removed_key, 5, "rbtree_add_and_remove first removed key");

	rbtree__destroy(skel);
}

static void test_rbtree_add_and_remove_array(void)
{
	LIBBPF_OPTS(bpf_test_run_opts, opts,
		    .data_in = &pkt_v4,
		    .data_size_in = sizeof(pkt_v4),
		    .repeat = 1,
	);
	struct rbtree *skel;
	int ret;

	skel = rbtree__open_and_load();
	if (!ASSERT_OK_PTR(skel, "rbtree__open_and_load"))
		return;

	ret = bpf_prog_test_run_opts(bpf_program__fd(skel->progs.rbtree_add_and_remove_array), &opts);
	ASSERT_OK(ret, "rbtree_add_and_remove_array");
	ASSERT_OK(opts.retval, "rbtree_add_and_remove_array retval");

	rbtree__destroy(skel);
}

static void test_rbtree_first_and_remove(void)
{
	LIBBPF_OPTS(bpf_test_run_opts, opts,
		    .data_in = &pkt_v4,
		    .data_size_in = sizeof(pkt_v4),
		    .repeat = 1,
	);
	struct rbtree *skel;
	int ret;

	skel = rbtree__open_and_load();
	if (!ASSERT_OK_PTR(skel, "rbtree__open_and_load"))
		return;

	ret = bpf_prog_test_run_opts(bpf_program__fd(skel->progs.rbtree_first_and_remove), &opts);
	ASSERT_OK(ret, "rbtree_first_and_remove");
	ASSERT_OK(opts.retval, "rbtree_first_and_remove retval");
	ASSERT_EQ(skel->data->first_data[0], 2, "rbtree_first_and_remove first rbtree_first()");
	ASSERT_EQ(skel->data->removed_key, 1, "rbtree_first_and_remove first removed key");
	ASSERT_EQ(skel->data->first_data[1], 4, "rbtree_first_and_remove second rbtree_first()");

	rbtree__destroy(skel);
}

static void test_rbtree_api_release_aliasing(void)
{
	LIBBPF_OPTS(bpf_test_run_opts, opts,
		    .data_in = &pkt_v4,
		    .data_size_in = sizeof(pkt_v4),
		    .repeat = 1,
	);
	struct rbtree *skel;
	int ret;

	skel = rbtree__open_and_load();
	if (!ASSERT_OK_PTR(skel, "rbtree__open_and_load"))
		return;

	ret = bpf_prog_test_run_opts(bpf_program__fd(skel->progs.rbtree_api_release_aliasing), &opts);
	ASSERT_OK(ret, "rbtree_api_release_aliasing");
	ASSERT_OK(opts.retval, "rbtree_api_release_aliasing retval");
	ASSERT_EQ(skel->data->first_data[0], 42, "rbtree_api_release_aliasing first rbtree_remove()");
	ASSERT_EQ(skel->data->first_data[1], -1, "rbtree_api_release_aliasing second rbtree_remove()");

	rbtree__destroy(skel);
}

void test_rbtree_success(void)
{
	if (test__start_subtest("rbtree_add_nodes"))
		test_rbtree_add_nodes();
	if (test__start_subtest("rbtree_add_nodes_nested"))
		test_rbtree_add_nodes_nested();
	if (test__start_subtest("rbtree_add_and_remove"))
		test_rbtree_add_and_remove();
	if (test__start_subtest("rbtree_add_and_remove_array"))
		test_rbtree_add_and_remove_array();
	if (test__start_subtest("rbtree_first_and_remove"))
		test_rbtree_first_and_remove();
	if (test__start_subtest("rbtree_api_release_aliasing"))
		test_rbtree_api_release_aliasing();
}

#define BTF_FAIL_TEST(suffix)									\
void test_rbtree_btf_fail__##suffix(void)							\
{												\
	struct rbtree_btf_fail__##suffix *skel;							\
												\
	skel = rbtree_btf_fail__##suffix##__open_and_load();					\
	if (!ASSERT_ERR_PTR(skel,								\
			    "rbtree_btf_fail__" #suffix "__open_and_load unexpected success"))	\
		rbtree_btf_fail__##suffix##__destroy(skel);					\
}

#define RUN_BTF_FAIL_TEST(suffix)				\
	if (test__start_subtest("rbtree_btf_fail__" #suffix))	\
		test_rbtree_btf_fail__##suffix();

BTF_FAIL_TEST(wrong_node_type);
BTF_FAIL_TEST(add_wrong_type);

void test_rbtree_btf_fail(void)
{
	RUN_BTF_FAIL_TEST(wrong_node_type);
	RUN_BTF_FAIL_TEST(add_wrong_type);
}

void test_rbtree_fail(void)
{
	RUN_TESTS(rbtree_fail);
}
