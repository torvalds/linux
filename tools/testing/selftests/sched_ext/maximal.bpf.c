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

#define DSQ_ID 0

s32 BPF_STRUCT_OPS(maximal_select_cpu, struct task_struct *p, s32 prev_cpu,
		   u64 wake_flags)
{
	return prev_cpu;
}

void BPF_STRUCT_OPS(maximal_enqueue, struct task_struct *p, u64 enq_flags)
{
	scx_bpf_dsq_insert(p, DSQ_ID, SCX_SLICE_DFL, enq_flags);
}

void BPF_STRUCT_OPS(maximal_dequeue, struct task_struct *p, u64 deq_flags)
{}

void BPF_STRUCT_OPS(maximal_dispatch, s32 cpu, struct task_struct *prev)
{
	scx_bpf_dsq_move_to_local(DSQ_ID);
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
	return scx_bpf_create_dsq(DSQ_ID, -1);
}

void BPF_STRUCT_OPS(maximal_exit, struct scx_exit_info *info)
{}

SEC(".struct_ops.link")
struct sched_ext_ops maximal_ops = {
	.select_cpu		= (void *) maximal_select_cpu,
	.enqueue		= (void *) maximal_enqueue,
	.dequeue		= (void *) maximal_dequeue,
	.dispatch		= (void *) maximal_dispatch,
	.runnable		= (void *) maximal_runnable,
	.running		= (void *) maximal_running,
	.stopping		= (void *) maximal_stopping,
	.quiescent		= (void *) maximal_quiescent,
	.yield			= (void *) maximal_yield,
	.core_sched_before	= (void *) maximal_core_sched_before,
	.set_weight		= (void *) maximal_set_weight,
	.set_cpumask		= (void *) maximal_set_cpumask,
	.update_idle		= (void *) maximal_update_idle,
	.cpu_acquire		= (void *) maximal_cpu_acquire,
	.cpu_release		= (void *) maximal_cpu_release,
	.cpu_online		= (void *) maximal_cpu_online,
	.cpu_offline		= (void *) maximal_cpu_offline,
	.init_task		= (void *) maximal_init_task,
	.enable			= (void *) maximal_enable,
	.exit_task		= (void *) maximal_exit_task,
	.disable		= (void *) maximal_disable,
	.cgroup_init		= (void *) maximal_cgroup_init,
	.cgroup_exit		= (void *) maximal_cgroup_exit,
	.cgroup_prep_move	= (void *) maximal_cgroup_prep_move,
	.cgroup_move		= (void *) maximal_cgroup_move,
	.cgroup_cancel_move	= (void *) maximal_cgroup_cancel_move,
	.cgroup_set_weight	= (void *) maximal_cgroup_set_weight,
	.init			= (void *) maximal_init,
	.exit			= (void *) maximal_exit,
	.name			= "maximal",
};
