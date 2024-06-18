// SPDX-License-Identifier: GPL-2.0
/* Copyright (C) 2021. Huawei Technologies Co., Ltd */
#include "vmlinux.h"
#include <bpf/bpf_helpers.h>
#include <bpf/bpf_tracing.h>

char _license[] SEC("license") = "GPL";

SEC("struct_ops/test_1")
int BPF_PROG(test_1, struct bpf_dummy_ops_state *state)
{
	int ret;

	/* Check that 'state' nullable status is detected correctly.
	 * If 'state' argument would be assumed non-null by verifier
	 * the code below would be deleted as dead (which it shouldn't).
	 * Hide it from the compiler behind 'asm' block to avoid
	 * unnecessary optimizations.
	 */
	asm volatile (
		"if %[state] != 0 goto +2;"
		"r0 = 0xf2f3f4f5;"
		"exit;"
	::[state]"p"(state));

	ret = state->val;
	state->val = 0x5a;
	return ret;
}

__u64 test_2_args[5];

SEC("struct_ops/test_2")
int BPF_PROG(test_2, struct bpf_dummy_ops_state *state, int a1, unsigned short a2,
	     char a3, unsigned long a4)
{
	test_2_args[0] = state->val;
	test_2_args[1] = a1;
	test_2_args[2] = a2;
	test_2_args[3] = a3;
	test_2_args[4] = a4;
	return 0;
}

SEC("struct_ops.s/test_sleepable")
int BPF_PROG(test_sleepable, struct bpf_dummy_ops_state *state)
{
	return 0;
}

SEC(".struct_ops")
struct bpf_dummy_ops dummy_1 = {
	.test_1 = (void *)test_1,
	.test_2 = (void *)test_2,
	.test_sleepable = (void *)test_sleepable,
};
