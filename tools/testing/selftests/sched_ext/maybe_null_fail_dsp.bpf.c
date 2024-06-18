/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (c) 2024 Meta Platforms, Inc. and affiliates.
 */

#include <scx/common.bpf.h>

char _license[] SEC("license") = "GPL";

u64 vtime_test;

void BPF_STRUCT_OPS(maybe_null_running, struct task_struct *p)
{}

void BPF_STRUCT_OPS(maybe_null_fail_dispatch, s32 cpu, struct task_struct *p)
{
	vtime_test = p->scx.dsq_vtime;
}

SEC(".struct_ops.link")
struct sched_ext_ops maybe_null_fail = {
	.dispatch               = maybe_null_fail_dispatch,
	.enable			= maybe_null_running,
	.name			= "maybe_null_fail_dispatch",
};
