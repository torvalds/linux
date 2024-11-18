/* SPDX-License-Identifier: GPL-2.0 */
/*
 * A scheduler that validates that enqueue flags are properly stored and
 * applied at dispatch time when a task is directly dispatched from
 * ops.select_cpu(). We validate this by using scx_bpf_dispatch_vtime(), and
 * making the test a very basic vtime scheduler.
 *
 * Copyright (c) 2024 Meta Platforms, Inc. and affiliates.
 * Copyright (c) 2024 David Vernet <dvernet@meta.com>
 * Copyright (c) 2024 Tejun Heo <tj@kernel.org>
 */

#include <scx/common.bpf.h>

char _license[] SEC("license") = "GPL";

volatile bool consumed;

static u64 vtime_now;

#define VTIME_DSQ 0

static inline bool vtime_before(u64 a, u64 b)
{
	return (s64)(a - b) < 0;
}

static inline u64 task_vtime(const struct task_struct *p)
{
	u64 vtime = p->scx.dsq_vtime;

	if (vtime_before(vtime, vtime_now - SCX_SLICE_DFL))
		return vtime_now - SCX_SLICE_DFL;
	else
		return vtime;
}

s32 BPF_STRUCT_OPS(select_cpu_vtime_select_cpu, struct task_struct *p,
		   s32 prev_cpu, u64 wake_flags)
{
	s32 cpu;

	cpu = scx_bpf_pick_idle_cpu(p->cpus_ptr, 0);
	if (cpu >= 0)
		goto ddsp;

	cpu = prev_cpu;
	scx_bpf_test_and_clear_cpu_idle(cpu);
ddsp:
	scx_bpf_dispatch_vtime(p, VTIME_DSQ, SCX_SLICE_DFL, task_vtime(p), 0);
	return cpu;
}

void BPF_STRUCT_OPS(select_cpu_vtime_dispatch, s32 cpu, struct task_struct *p)
{
	if (scx_bpf_consume(VTIME_DSQ))
		consumed = true;
}

void BPF_STRUCT_OPS(select_cpu_vtime_running, struct task_struct *p)
{
	if (vtime_before(vtime_now, p->scx.dsq_vtime))
		vtime_now = p->scx.dsq_vtime;
}

void BPF_STRUCT_OPS(select_cpu_vtime_stopping, struct task_struct *p,
		    bool runnable)
{
	p->scx.dsq_vtime += (SCX_SLICE_DFL - p->scx.slice) * 100 / p->scx.weight;
}

void BPF_STRUCT_OPS(select_cpu_vtime_enable, struct task_struct *p)
{
	p->scx.dsq_vtime = vtime_now;
}

s32 BPF_STRUCT_OPS_SLEEPABLE(select_cpu_vtime_init)
{
	return scx_bpf_create_dsq(VTIME_DSQ, -1);
}

SEC(".struct_ops.link")
struct sched_ext_ops select_cpu_vtime_ops = {
	.select_cpu		= select_cpu_vtime_select_cpu,
	.dispatch		= select_cpu_vtime_dispatch,
	.running		= select_cpu_vtime_running,
	.stopping		= select_cpu_vtime_stopping,
	.enable			= select_cpu_vtime_enable,
	.init			= select_cpu_vtime_init,
	.name			= "select_cpu_vtime",
	.timeout_ms		= 1000U,
};
