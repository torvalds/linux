// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2023 Meta Platforms, Inc. and affiliates.
 * Copyright (c) 2023 David Vernet <dvernet@meta.com>
 * Copyright (c) 2023 Tejun Heo <tj@kernel.org>
 */

#include <scx/common.bpf.h>

char _license[] SEC("license") = "GPL";

UEI_DEFINE(uei);

s32 BPF_STRUCT_OPS(enq_select_cpu_select_cpu, struct task_struct *p,
		   s32 prev_cpu, u64 wake_flags)
{
	/* Bounce all tasks to ops.enqueue() */
	return prev_cpu;
}

void BPF_STRUCT_OPS(enq_select_cpu_enqueue, struct task_struct *p,
		    u64 enq_flags)
{
	s32 cpu, prev_cpu = scx_bpf_task_cpu(p);
	bool found = false;

	cpu = scx_bpf_select_cpu_dfl(p, prev_cpu, 0, &found);
	if (found) {
		scx_bpf_dsq_insert(p, SCX_DSQ_LOCAL_ON | cpu, SCX_SLICE_DFL, enq_flags);
		return;
	}

	scx_bpf_dsq_insert(p, SCX_DSQ_GLOBAL, SCX_SLICE_DFL, enq_flags);
}

void BPF_STRUCT_OPS(enq_select_cpu_exit, struct scx_exit_info *ei)
{
	UEI_RECORD(uei, ei);
}

struct task_cpu_arg {
	pid_t pid;
};

SEC("syscall")
int select_cpu_from_user(struct task_cpu_arg *input)
{
	struct task_struct *p;
	bool found = false;
	s32 cpu;

	p = bpf_task_from_pid(input->pid);
	if (!p)
		return -EINVAL;

	bpf_rcu_read_lock();
	cpu = scx_bpf_select_cpu_dfl(p, bpf_get_smp_processor_id(), 0, &found);
	if (!found)
		cpu = -EBUSY;
	bpf_rcu_read_unlock();

	bpf_task_release(p);

	return cpu;
}

SEC(".struct_ops.link")
struct sched_ext_ops enq_select_cpu_ops = {
	.select_cpu		= (void *)enq_select_cpu_select_cpu,
	.enqueue		= (void *)enq_select_cpu_enqueue,
	.exit			= (void *)enq_select_cpu_exit,
	.name			= "enq_select_cpu",
	.timeout_ms		= 1000U,
};
