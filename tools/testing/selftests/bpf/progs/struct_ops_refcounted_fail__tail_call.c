// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2025 Meta Platforms, Inc. and affiliates. */
#include <vmlinux.h>
#include <bpf/bpf_tracing.h>
#include "../test_kmods/bpf_testmod.h"
#include "bpf_misc.h"

char _license[] SEC("license") = "GPL";

struct {
	__uint(type, BPF_MAP_TYPE_PROG_ARRAY);
	__uint(max_entries, 1);
	__uint(key_size, sizeof(__u32));
	__uint(value_size, sizeof(__u32));
} prog_array SEC(".maps");

/* Test that the verifier rejects a program with referenced kptr arguments
 * that tail call
 */
SEC("struct_ops/test_refcounted")
__failure __msg("program with __ref argument cannot tail call")
int refcounted_fail__tail_call(unsigned long long *ctx)
{
	struct task_struct *task = (struct task_struct *)ctx[1];

	bpf_task_release(task);
	bpf_tail_call(ctx, &prog_array, 0);

	return 0;
}

SEC(".struct_ops.link")
struct bpf_testmod_ops testmod_ref_acquire = {
	.test_refcounted = (void *)refcounted_fail__tail_call,
};

