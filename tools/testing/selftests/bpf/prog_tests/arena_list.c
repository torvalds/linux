// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2024 Meta Platforms, Inc. and affiliates. */
#include <test_progs.h>
#include <sys/mman.h>
#include <network_helpers.h>
#include <sys/user.h>
#ifndef PAGE_SIZE /* on some archs it comes in sys/user.h */
#include <unistd.h>
#define PAGE_SIZE getpagesize()
#endif

#include "bpf_arena_list.h"
#include "arena_list.skel.h"

struct elem {
	struct arena_list_node node;
	__u64 value;
};

static int list_sum(struct arena_list_head *head)
{
	struct elem __arena *n;
	int sum = 0;

	list_for_each_entry(n, head, node)
		sum += n->value;
	return sum;
}

static void test_arena_list_add_del(int cnt)
{
	LIBBPF_OPTS(bpf_test_run_opts, opts);
	struct arena_list *skel;
	int expected_sum = (u64)cnt * (cnt - 1) / 2;
	int ret, sum;

	skel = arena_list__open_and_load();
	if (!ASSERT_OK_PTR(skel, "arena_list__open_and_load"))
		return;

	skel->bss->cnt = cnt;
	ret = bpf_prog_test_run_opts(bpf_program__fd(skel->progs.arena_list_add), &opts);
	ASSERT_OK(ret, "ret_add");
	ASSERT_OK(opts.retval, "retval");
	if (skel->bss->skip) {
		printf("%s:SKIP:compiler doesn't support arena_cast\n", __func__);
		test__skip();
		goto out;
	}
	sum = list_sum(skel->bss->list_head);
	ASSERT_EQ(sum, expected_sum, "sum of elems");
	ASSERT_EQ(skel->arena->arena_sum, expected_sum, "__arena sum of elems");
	ASSERT_EQ(skel->arena->test_val, cnt + 1, "num of elems");

	ret = bpf_prog_test_run_opts(bpf_program__fd(skel->progs.arena_list_del), &opts);
	ASSERT_OK(ret, "ret_del");
	sum = list_sum(skel->bss->list_head);
	ASSERT_EQ(sum, 0, "sum of list elems after del");
	ASSERT_EQ(skel->bss->list_sum, expected_sum, "sum of list elems computed by prog");
	ASSERT_EQ(skel->arena->arena_sum, expected_sum, "__arena sum of elems");
out:
	arena_list__destroy(skel);
}

void test_arena_list(void)
{
	if (test__start_subtest("arena_list_1"))
		test_arena_list_add_del(1);
	if (test__start_subtest("arena_list_1000"))
		test_arena_list_add_del(1000);
}
