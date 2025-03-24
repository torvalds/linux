// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2024 Meta Platforms, Inc. and affiliates. */
#include <test_progs.h>

#include "test_build_id.skel.h"

static char build_id[BPF_BUILD_ID_SIZE];
static int build_id_sz;

static void print_stack(struct bpf_stack_build_id *stack, int frame_cnt)
{
	int i, j;

	for (i = 0; i < frame_cnt; i++) {
		printf("FRAME #%02d: ", i);
		switch (stack[i].status) {
		case BPF_STACK_BUILD_ID_EMPTY:
			printf("<EMPTY>\n");
			break;
		case BPF_STACK_BUILD_ID_VALID:
			printf("BUILD ID = ");
			for (j = 0; j < BPF_BUILD_ID_SIZE; j++)
				printf("%02hhx", (unsigned)stack[i].build_id[j]);
			printf(" OFFSET = %llx", (unsigned long long)stack[i].offset);
			break;
		case BPF_STACK_BUILD_ID_IP:
			printf("IP = %llx", (unsigned long long)stack[i].ip);
			break;
		default:
			printf("UNEXPECTED STATUS %d ", stack[i].status);
			break;
		}
		printf("\n");
	}
}

static void subtest_nofault(bool build_id_resident)
{
	struct test_build_id *skel;
	struct bpf_stack_build_id *stack;
	int frame_cnt;

	skel = test_build_id__open_and_load();
	if (!ASSERT_OK_PTR(skel, "skel_open"))
		return;

	skel->links.uprobe_nofault = bpf_program__attach(skel->progs.uprobe_nofault);
	if (!ASSERT_OK_PTR(skel->links.uprobe_nofault, "link"))
		goto cleanup;

	if (build_id_resident)
		ASSERT_OK(system("./uprobe_multi uprobe-paged-in"), "trigger_uprobe");
	else
		ASSERT_OK(system("./uprobe_multi uprobe-paged-out"), "trigger_uprobe");

	if (!ASSERT_GT(skel->bss->res_nofault, 0, "res"))
		goto cleanup;

	stack = skel->bss->stack_nofault;
	frame_cnt = skel->bss->res_nofault / sizeof(struct bpf_stack_build_id);
	if (env.verbosity >= VERBOSE_NORMAL)
		print_stack(stack, frame_cnt);

	if (build_id_resident) {
		ASSERT_EQ(stack[0].status, BPF_STACK_BUILD_ID_VALID, "build_id_status");
		ASSERT_EQ(memcmp(stack[0].build_id, build_id, build_id_sz), 0, "build_id_match");
	} else {
		ASSERT_EQ(stack[0].status, BPF_STACK_BUILD_ID_IP, "build_id_status");
	}

cleanup:
	test_build_id__destroy(skel);
}

static void subtest_sleepable(void)
{
	struct test_build_id *skel;
	struct bpf_stack_build_id *stack;
	int frame_cnt;

	skel = test_build_id__open_and_load();
	if (!ASSERT_OK_PTR(skel, "skel_open"))
		return;

	skel->links.uprobe_sleepable = bpf_program__attach(skel->progs.uprobe_sleepable);
	if (!ASSERT_OK_PTR(skel->links.uprobe_sleepable, "link"))
		goto cleanup;

	/* force build ID to not be paged in */
	ASSERT_OK(system("./uprobe_multi uprobe-paged-out"), "trigger_uprobe");

	if (!ASSERT_GT(skel->bss->res_sleepable, 0, "res"))
		goto cleanup;

	stack = skel->bss->stack_sleepable;
	frame_cnt = skel->bss->res_sleepable / sizeof(struct bpf_stack_build_id);
	if (env.verbosity >= VERBOSE_NORMAL)
		print_stack(stack, frame_cnt);

	ASSERT_EQ(stack[0].status, BPF_STACK_BUILD_ID_VALID, "build_id_status");
	ASSERT_EQ(memcmp(stack[0].build_id, build_id, build_id_sz), 0, "build_id_match");

cleanup:
	test_build_id__destroy(skel);
}

void serial_test_build_id(void)
{
	build_id_sz = read_build_id("uprobe_multi", build_id, sizeof(build_id));
	ASSERT_EQ(build_id_sz, BPF_BUILD_ID_SIZE, "parse_build_id");

	if (test__start_subtest("nofault-paged-out"))
		subtest_nofault(false /* not resident */);
	if (test__start_subtest("nofault-paged-in"))
		subtest_nofault(true /* resident */);
	if (test__start_subtest("sleepable"))
		subtest_sleepable();
}
