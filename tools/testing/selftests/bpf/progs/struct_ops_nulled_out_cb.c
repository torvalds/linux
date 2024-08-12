// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2024 Meta Platforms, Inc. and affiliates. */
#include <vmlinux.h>
#include <bpf/bpf_tracing.h>
#include "../bpf_testmod/bpf_testmod.h"

char _license[] SEC("license") = "GPL";

int rand;
int arr[1];

SEC("struct_ops/test_1")
int BPF_PROG(test_1_turn_off)
{
	return arr[rand]; /* potentially way out of range access */
}

SEC(".struct_ops.link")
struct bpf_testmod_ops ops = {
	.test_1 = (void *)test_1_turn_off,
};

