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

s32 BPF_STRUCT_OPS(select_cpu_dispatch_select_cpu, struct task_struct *p,
		   s32 prev_cpu, u64 wake_flags)
{
	u64 dsq_id = SCX_DSQ_LOCAL;
	s32 cpu = prev_cpu;

	if (scx_bpf_test_and_clear_cpu_idle(cpu))
		goto dispatch;

	cpu = scx_bpf_pick_idle_cpu(p->cpus_ptr, 0);
	if (cpu >= 0)
		goto dispatch;

	dsq_id = SCX_DSQ_GLOBAL;
	cpu = prev_cpu;

dispatch:
	scx_bpf_dispatch(p, dsq_id, SCX_SLICE_DFL, 0);
	return cpu;
}

SEC(".struct_ops.link")
struct sched_ext_ops select_cpu_dispatch_ops = {
	.select_cpu		= select_cpu_dispatch_select_cpu,
	.name			= "select_cpu_dispatch",
	.timeout_ms		= 1000U,
};
