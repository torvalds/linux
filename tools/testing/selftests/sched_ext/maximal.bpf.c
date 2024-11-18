/* SPDX-License-Identifier: GPL-2.0 */
/*
 * A scheduler with every callback defined.
 *
 * This scheduler defines every callback.
 *
 * Copyright (c) 2024 Meta Platforms, Inc. and affiliates.
 * Copyright (c) 2024 David Vernet <dvernet@meta.com>
 */

#include <scx/common.bpf.h>

char _license[] SEC("license") = "GPL";

s32 BPF_STRUCT_OPS(maximal_select_cpu, struct task_struct *p, s32 prev_cpu,
		   u64 wake_flags)
{
	return prev_cpu;
}

void BPF_STRUCT_OPS(maximal_enqueue, struct task_struct *p, u64 enq_flags)
{
	scx_bpf_dispatch(p, SCX_DSQ_GLOBAL, SCX_SLICE_DFL, enq_flags);
}

void BPF_STRUCT_OPS(maximal_dequeue, struct task_struct *p, u64 deq_flags)
{}

void BPF_STRUCT_OPS(maximal_dispatch, s32 cpu, struct task_struct *prev)
{
	scx_bpf_consume(SCX_DSQ_GLOBAL);
}

void BPF_STRUCT_OPS(maximal_runnable, struct task_struct *p, u64 enq_flags)
{}

void BPF_STRUCT_OPS(maximal_running, struct task_struct *p)
{}

void BPF_STRUCT_OPS(maximal_stopping, struct task_struct *p, bool runnable)
{}

void BPF_STRUCT_OPS(maximal_quiescent, struct task_struct *p, u64 deq_flags)
{}

bool BPF_STRUCT_OPS(maximal_yield, struct task_struct *from,
		    struct task_struct *to)
{
	return false;
}

bool BPF_STRUCT_OPS(maximal_core_sched_before, struct task_struct *a,
		    struct task_struct *b)
{
	return false;
}

void BPF_STRUCT_OPS(maximal_set_weight, struct task_struct *p, u32 weight)
{}

void BPF_STRUCT_OPS(maximal_set_cpumask, struct task_struct *p,
		    const struct cpumask *cpumask)
{}

void BPF_STRUCT_OPS(maximal_update_idle, s32 cpu, bool idle)
{}

void BPF_STRUCT_OPS(maximal_cpu_acquire, s32 cpu,
		    struct scx_cpu_acquire_args *args)
{}

void BPF_STRUCT_OPS(maximal_cpu_release, s32 cpu,
		    struct scx_cpu_release_args *args)
{}

void BPF_STRUCT_OPS(maximal_cpu_online, s32 cpu)
{}

void BPF_STRUCT_OPS(maximal_cpu_offline, s32 cpu)
{}

s32 BPF_STRUCT_OPS(maximal_init_task, struct task_struct *p,
		   struct scx_init_task_args *args)
{
	return 0;
}

void BPF_STRUCT_OPS(maximal_enable, struct task_struct *p)
{}

void BPF_STRUCT_OPS(maximal_exit_task, struct task_struct *p,
		    struct scx_exit_task_args *args)
{}

void BPF_STRUCT_OPS(maximal_disable, struct task_struct *p)
{}

s32 BPF_STRUCT_OPS(maximal_cgroup_init, struct cgroup *cgrp,
		   struct scx_cgroup_init_args *args)
{
	return 0;
}

void BPF_STRUCT_OPS(maximal_cgroup_exit, struct cgroup *cgrp)
{}

s32 BPF_STRUCT_OPS(maximal_cgroup_prep_move, struct task_struct *p,
		   struct cgroup *from, struct cgroup *to)
{
	return 0;
}

void BPF_STRUCT_OPS(maximal_cgroup_move, struct task_struct *p,
		    struct cgroup *from, struct cgroup *to)
{}

void BPF_STRUCT_OPS(maximal_cgroup_cancel_move, struct task_struct *p,
	       struct cgroup *from, struct cgroup *to)
{}

void BPF_STRUCT_OPS(maximal_cgroup_set_weight, struct cgroup *cgrp, u32 weight)
{}

s32 BPF_STRUCT_OPS_SLEEPABLE(maximal_init)
{
	return 0;
}

void BPF_STRUCT_OPS(maximal_exit, struct scx_exit_info *info)
{}

SEC(".struct_ops.link")
struct sched_ext_ops maximal_ops = {
	.select_cpu		= maximal_select_cpu,
	.enqueue		= maximal_enqueue,
	.dequeue		= maximal_dequeue,
	.dispatch		= maximal_dispatch,
	.runnable		= maximal_runnable,
	.running		= maximal_running,
	.stopping		= maximal_stopping,
	.quiescent		= maximal_quiescent,
	.yield			= maximal_yield,
	.core_sched_before	= maximal_core_sched_before,
	.set_weight		= maximal_set_weight,
	.set_cpumask		= maximal_set_cpumask,
	.update_idle		= maximal_update_idle,
	.cpu_acquire		= maximal_cpu_acquire,
	.cpu_release		= maximal_cpu_release,
	.cpu_online		= maximal_cpu_online,
	.cpu_offline		= maximal_cpu_offline,
	.init_task		= maximal_init_task,
	.enable			= maximal_enable,
	.exit_task		= maximal_exit_task,
	.disable		= maximal_disable,
	.cgroup_init		= maximal_cgroup_init,
	.cgroup_exit		= maximal_cgroup_exit,
	.cgroup_prep_move	= maximal_cgroup_prep_move,
	.cgroup_move		= maximal_cgroup_move,
	.cgroup_cancel_move	= maximal_cgroup_cancel_move,
	.cgroup_set_weight	= maximal_cgroup_set_weight,
	.init			= maximal_init,
	.exit			= maximal_exit,
	.name			= "maximal",
};
