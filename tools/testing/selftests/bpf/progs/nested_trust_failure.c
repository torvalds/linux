// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2023 Meta Platforms, Inc. and affiliates. */

#include <vmlinux.h>
#include <bpf/bpf_tracing.h>
#include <bpf/bpf_helpers.h>
#include "bpf_misc.h"

#include "nested_trust_common.h"

char _license[] SEC("license") = "GPL";

/* Prototype for all of the program trace events below:
 *
 * TRACE_EVENT(task_newtask,
 *         TP_PROTO(struct task_struct *p, u64 clone_flags)
 */

SEC("tp_btf/task_newtask")
__failure __msg("R2 must be")
int BPF_PROG(test_invalid_nested_user_cpus, struct task_struct *task, u64 clone_flags)
{
	bpf_cpumask_test_cpu(0, task->user_cpus_ptr);
	return 0;
}

SEC("tp_btf/task_newtask")
__failure __msg("R1 must have zero offset when passed to release func or trusted arg to kfunc")
int BPF_PROG(test_invalid_nested_offset, struct task_struct *task, u64 clone_flags)
{
	bpf_cpumask_first_zero(&task->cpus_mask);
	return 0;
}
