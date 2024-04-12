// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2024 Meta Platforms, Inc. and affiliates. */
#include <vmlinux.h>
#include <bpf/bpf_tracing.h>
#include "../bpf_testmod/bpf_testmod.h"

char _license[] SEC("license") = "GPL";

pid_t tgid = 0;

SEC("struct_ops/test_maybe_null_struct_ptr")
int BPF_PROG(test_maybe_null_struct_ptr, int dummy,
	     struct task_struct *task)
{
	tgid = task->tgid;

	return 0;
}

SEC(".struct_ops.link")
struct bpf_testmod_ops testmod_struct_ptr = {
	.test_maybe_null = (void *)test_maybe_null_struct_ptr,
};

