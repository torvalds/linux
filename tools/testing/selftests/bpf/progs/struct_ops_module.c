// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2024 Meta Platforms, Inc. and affiliates. */
#include <vmlinux.h>
#include <bpf/bpf_helpers.h>
#include <bpf/bpf_tracing.h>
#include "../bpf_testmod/bpf_testmod.h"

char _license[] SEC("license") = "GPL";

int test_1_result = 0;
int test_2_result = 0;

SEC("struct_ops/test_1")
int BPF_PROG(test_1)
{
	test_1_result = 0xdeadbeef;
	return 0;
}

SEC("struct_ops/test_2")
void BPF_PROG(test_2, int a, int b)
{
	test_2_result = a + b;
}

SEC("?struct_ops/test_3")
int BPF_PROG(test_3, int a, int b)
{
	test_2_result = a + b + 3;
	return a + b + 3;
}

SEC(".struct_ops.link")
struct bpf_testmod_ops testmod_1 = {
	.test_1 = (void *)test_1,
	.test_2 = (void *)test_2,
	.data = 0x1,
};

SEC("struct_ops/test_2")
void BPF_PROG(test_2_v2, int a, int b)
{
	test_2_result = a * b;
}

struct bpf_testmod_ops___v2 {
	int (*test_1)(void);
	void (*test_2)(int a, int b);
	int (*test_maybe_null)(int dummy, struct task_struct *task);
};

SEC(".struct_ops.link")
struct bpf_testmod_ops___v2 testmod_2 = {
	.test_1 = (void *)test_1,
	.test_2 = (void *)test_2_v2,
};

struct bpf_testmod_ops___zeroed {
	int (*test_1)(void);
	void (*test_2)(int a, int b);
	int (*test_maybe_null)(int dummy, struct task_struct *task);
	void (*zeroed_op)(int a, int b);
	int zeroed;
};

SEC("struct_ops/test_3")
int BPF_PROG(zeroed_op)
{
	return 1;
}

SEC(".struct_ops.link")
struct bpf_testmod_ops___zeroed testmod_zeroed = {
	.test_1 = (void *)test_1,
	.test_2 = (void *)test_2_v2,
	.zeroed_op = (void *)zeroed_op,
};

struct bpf_testmod_ops___incompatible {
	int (*test_1)(void);
	void (*test_2)(int *a);
	int data;
};

SEC(".struct_ops.link")
struct bpf_testmod_ops___incompatible testmod_incompatible = {
	.test_1 = (void *)test_1,
	.test_2 = (void *)test_2,
	.data = 3,
};
