/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (c) 2023 Meta Platforms, Inc. and affiliates.
 * Copyright (c) 2023 David Vernet <dvernet@meta.com>
 * Copyright (c) 2023 Tejun Heo <tj@kernel.org>
 */

#include <scx/common.bpf.h>

char _license[] SEC("license") = "GPL";

/* Manually specify the signature until the kfunc is added to the scx repo. */
s32 scx_bpf_select_cpu_dfl(struct task_struct *p, s32 prev_cpu, u64 wake_flags,
			   bool *found) __ksym;

s32 BPF_STRUCT_OPS(enq_select_cpu_fails_select_cpu, struct task_struct *p,
		   s32 prev_cpu, u64 wake_flags)
{
	return prev_cpu;
}

void BPF_STRUCT_OPS(enq_select_cpu_fails_enqueue, struct task_struct *p,
		    u64 enq_flags)
{
	/*
	 * Need to initialize the variable or the verifier will fail to load.
	 * Improving these semantics is actively being worked on.
	 */
	bool found = false;

	/* Can only call from ops.select_cpu() */
	scx_bpf_select_cpu_dfl(p, 0, 0, &found);

	scx_bpf_dispatch(p, SCX_DSQ_GLOBAL, SCX_SLICE_DFL, enq_flags);
}

SEC(".struct_ops.link")
struct sched_ext_ops enq_select_cpu_fails_ops = {
	.select_cpu		= enq_select_cpu_fails_select_cpu,
	.enqueue		= enq_select_cpu_fails_enqueue,
	.name			= "enq_select_cpu_fails",
	.timeout_ms		= 1000U,
};
