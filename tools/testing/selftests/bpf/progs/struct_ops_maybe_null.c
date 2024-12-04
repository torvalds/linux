// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2024 Meta Platforms, Inc. and affiliates. */
#include <vmlinux.h>
#include <bpf/bpf_tracing.h>
#include "../test_kmods/bpf_testmod.h"

char _license[] SEC("license") = "GPL";

pid_t tgid = 0;

/* This is a test BPF program that uses struct_ops to access an argument
 * that may be NULL. This is a test for the verifier to ensure that it can
 * rip PTR_MAYBE_NULL correctly.
 */
SEC("struct_ops/test_maybe_null")
int BPF_PROG(test_maybe_null, int dummy,
	     struct task_struct *task)
{
	if (task)
		tgid = task->tgid;

	return 0;
}

SEC(".struct_ops.link")
struct bpf_testmod_ops testmod_1 = {
	.test_maybe_null = (void *)test_maybe_null,
};

