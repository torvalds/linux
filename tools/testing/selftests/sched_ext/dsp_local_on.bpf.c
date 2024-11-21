/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (c) 2024 Meta Platforms, Inc. and affiliates.
 * Copyright (c) 2024 David Vernet <dvernet@meta.com>
 */
#include <scx/common.bpf.h>

char _license[] SEC("license") = "GPL";
const volatile s32 nr_cpus;

UEI_DEFINE(uei);

struct {
	__uint(type, BPF_MAP_TYPE_QUEUE);
	__uint(max_entries, 8192);
	__type(value, s32);
} queue SEC(".maps");

s32 BPF_STRUCT_OPS(dsp_local_on_select_cpu, struct task_struct *p,
		   s32 prev_cpu, u64 wake_flags)
{
	return prev_cpu;
}

void BPF_STRUCT_OPS(dsp_local_on_enqueue, struct task_struct *p,
		    u64 enq_flags)
{
	s32 pid = p->pid;

	if (bpf_map_push_elem(&queue, &pid, 0))
		scx_bpf_error("Failed to enqueue %s[%d]", p->comm, p->pid);
}

void BPF_STRUCT_OPS(dsp_local_on_dispatch, s32 cpu, struct task_struct *prev)
{
	s32 pid, target;
	struct task_struct *p;

	if (bpf_map_pop_elem(&queue, &pid))
		return;

	p = bpf_task_from_pid(pid);
	if (!p)
		return;

	target = bpf_get_prandom_u32() % nr_cpus;

	scx_bpf_dsq_insert(p, SCX_DSQ_LOCAL_ON | target, SCX_SLICE_DFL, 0);
	bpf_task_release(p);
}

void BPF_STRUCT_OPS(dsp_local_on_exit, struct scx_exit_info *ei)
{
	UEI_RECORD(uei, ei);
}

SEC(".struct_ops.link")
struct sched_ext_ops dsp_local_on_ops = {
	.select_cpu		= (void *) dsp_local_on_select_cpu,
	.enqueue		= (void *) dsp_local_on_enqueue,
	.dispatch		= (void *) dsp_local_on_dispatch,
	.exit			= (void *) dsp_local_on_exit,
	.name			= "dsp_local_on",
	.timeout_ms		= 1000U,
};
