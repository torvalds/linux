/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (c) 2024 Meta Platforms, Inc. and affiliates.
 */

#include <scx/common.bpf.h>

char _license[] SEC("license") = "GPL";

u64 vtime_test;

void BPF_STRUCT_OPS(maybe_null_running, struct task_struct *p)
{}

void BPF_STRUCT_OPS(maybe_null_success_dispatch, s32 cpu, struct task_struct *p)
{
	if (p != NULL)
		vtime_test = p->scx.dsq_vtime;
}

bool BPF_STRUCT_OPS(maybe_null_success_yield, struct task_struct *from,
		    struct task_struct *to)
{
	if (to)
		bpf_printk("Yielding to %s[%d]", to->comm, to->pid);

	return false;
}

SEC(".struct_ops.link")
struct sched_ext_ops maybe_null_success = {
	.dispatch               = (void *) maybe_null_success_dispatch,
	.yield			= (void *) maybe_null_success_yield,
	.enable			= (void *) maybe_null_running,
	.name			= "minimal",
};
