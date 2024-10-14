/* SPDX-License-Identifier: GPL-2.0 */
/*
 * A scheduler that validates the behavior of direct dispatching with a default
 * select_cpu implementation.
 *
 * Copyright (c) 2023 Meta Platforms, Inc. and affiliates.
 * Copyright (c) 2023 David Vernet <dvernet@meta.com>
 * Copyright (c) 2023 Tejun Heo <tj@kernel.org>
 */

#include <scx/common.bpf.h>

char _license[] SEC("license") = "GPL";

UEI_DEFINE(uei);

s32 BPF_STRUCT_OPS(select_cpu_dispatch_bad_dsq_select_cpu, struct task_struct *p,
		   s32 prev_cpu, u64 wake_flags)
{
	/* Dispatching to a random DSQ should fail. */
	scx_bpf_dispatch(p, 0xcafef00d, SCX_SLICE_DFL, 0);

	return prev_cpu;
}

void BPF_STRUCT_OPS(select_cpu_dispatch_bad_dsq_exit, struct scx_exit_info *ei)
{
	UEI_RECORD(uei, ei);
}

SEC(".struct_ops.link")
struct sched_ext_ops select_cpu_dispatch_bad_dsq_ops = {
	.select_cpu		= select_cpu_dispatch_bad_dsq_select_cpu,
	.exit			= select_cpu_dispatch_bad_dsq_exit,
	.name			= "select_cpu_dispatch_bad_dsq",
	.timeout_ms		= 1000U,
};
