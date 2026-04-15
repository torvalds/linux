// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2026 Varun R Mallya */

#include <vmlinux.h>
#include <bpf/bpf_helpers.h>
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

SEC("struct_ops/test_refcounted_multi")
__failure __msg("program with __ref argument cannot tail call")
int test_refcounted_multi(unsigned long long *ctx)
{
	/* ctx[2] is used because the refcounted variable is the third argument */
	struct task_struct *refcounted_task = (struct task_struct *)ctx[2];

	bpf_task_release(refcounted_task);
	bpf_tail_call(ctx, &prog_array, 0);

	return 0;
}

SEC(".struct_ops.link")
struct bpf_testmod_ops testmod_ref_acquire = {
	.test_refcounted_multi = (void *)test_refcounted_multi,
};
