/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (c) 2024 Meta Platforms, Inc. and affiliates.
 * Copyright (c) 2024 David Vernet <dvernet@meta.com>
 * Copyright (c) 2024 Tejun Heo <tj@kernel.org>
 */
#include <scx/common.bpf.h>

char _license[] SEC("license") = "GPL";

UEI_DEFINE(uei);

s32 BPF_STRUCT_OPS(ddsp_bogus_dsq_fail_select_cpu, struct task_struct *p,
		   s32 prev_cpu, u64 wake_flags)
{
	s32 cpu = scx_bpf_pick_idle_cpu(p->cpus_ptr, 0);

	if (cpu >= 0) {
		/*
		 * If we dispatch to a bogus DSQ that will fall back to the
		 * builtin global DSQ, we fail gracefully.
		 */
		scx_bpf_dsq_insert_vtime(p, 0xcafef00d, SCX_SLICE_DFL,
				       p->scx.dsq_vtime, 0);
		return cpu;
	}

	return prev_cpu;
}

void BPF_STRUCT_OPS(ddsp_bogus_dsq_fail_exit, struct scx_exit_info *ei)
{
	UEI_RECORD(uei, ei);
}

SEC(".struct_ops.link")
struct sched_ext_ops ddsp_bogus_dsq_fail_ops = {
	.select_cpu		= (void *) ddsp_bogus_dsq_fail_select_cpu,
	.exit			= (void *) ddsp_bogus_dsq_fail_exit,
	.name			= "ddsp_bogus_dsq_fail",
	.timeout_ms		= 1000U,
};
