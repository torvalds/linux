// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2024 Meta Platforms, Inc. and affiliates. */
#include <vmlinux.h>
#include <bpf/bpf_helpers.h>
#include <bpf/bpf_tracing.h>
#include "../bpf_testmod/bpf_testmod.h"

char _license[] SEC("license") = "GPL";

int test_2_result = 0;

SEC("struct_ops/test_1")
int BPF_PROG(test_1)
{
	return 0xdeadbeef;
}

SEC("struct_ops/test_2")
void BPF_PROG(test_2, int a, int b)
{
	test_2_result = a + b;
}

SEC("struct_ops/test_3")
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

