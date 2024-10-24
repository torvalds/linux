/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (c) 2024 Meta Platforms, Inc. and affiliates.
 */

#include <scx/common.bpf.h>

char _license[] SEC("license") = "GPL";

u64 vtime_test;

void BPF_STRUCT_OPS(maybe_null_running, struct task_struct *p)
{}

bool BPF_STRUCT_OPS(maybe_null_fail_yield, struct task_struct *from,
		    struct task_struct *to)
{
	bpf_printk("Yielding to %s[%d]", to->comm, to->pid);

	return false;
}

SEC(".struct_ops.link")
struct sched_ext_ops maybe_null_fail = {
	.yield			= (void *) maybe_null_fail_yield,
	.enable			= (void *) maybe_null_running,
	.name			= "maybe_null_fail_yield",
};
