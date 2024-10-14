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

bool saw_local = false;

static bool task_is_test(const struct task_struct *p)
{
	return !bpf_strncmp(p->comm, 9, "select_cpu");
}

void BPF_STRUCT_OPS(select_cpu_dfl_enqueue, struct task_struct *p,
		    u64 enq_flags)
{
	const struct cpumask *idle_mask = scx_bpf_get_idle_cpumask();

	if (task_is_test(p) &&
	    bpf_cpumask_test_cpu(scx_bpf_task_cpu(p), idle_mask)) {
		saw_local = true;
	}
	scx_bpf_put_idle_cpumask(idle_mask);

	scx_bpf_dispatch(p, SCX_DSQ_GLOBAL, SCX_SLICE_DFL, enq_flags);
}

SEC(".struct_ops.link")
struct sched_ext_ops select_cpu_dfl_ops = {
	.enqueue		= select_cpu_dfl_enqueue,
	.name			= "select_cpu_dfl",
};
