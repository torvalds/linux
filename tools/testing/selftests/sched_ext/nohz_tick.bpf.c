// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES
 *
 * Exercise tick dependency transitions between infinite and finite slices.
 */
#include <scx/common.bpf.h>

char _license[] SEC("license") = "GPL";

const volatile s32 test_cpu;
bool finite_phase;
u64 nr_inf_running;
u64 nr_finite_running;
u64 nr_finite_ticks;

UEI_DEFINE(uei);

s32 BPF_STRUCT_OPS(nohz_tick_select_cpu, struct task_struct *p, s32 prev_cpu,
		   u64 wake_flags)
{
	return prev_cpu;
}

void BPF_STRUCT_OPS(nohz_tick_enqueue, struct task_struct *p, u64 enq_flags)
{
	u64 slice = finite_phase ? 1000000ULL : SCX_SLICE_INF;

	scx_bpf_dsq_insert(p, SCX_DSQ_GLOBAL, slice, enq_flags);
	if (enq_flags & SCX_ENQ_LAST)
		scx_bpf_kick_cpu(test_cpu, SCX_KICK_IDLE);
}

void BPF_STRUCT_OPS(nohz_tick_running, struct task_struct *p)
{
	if (bpf_get_smp_processor_id() != test_cpu)
		return;

	if (finite_phase)
		__sync_fetch_and_add(&nr_finite_running, 1);
	else
		__sync_fetch_and_add(&nr_inf_running, 1);
}

void BPF_STRUCT_OPS(nohz_tick_tick, struct task_struct *p)
{
	if (bpf_get_smp_processor_id() == test_cpu && finite_phase)
		__sync_fetch_and_add(&nr_finite_ticks, 1);
}

void BPF_STRUCT_OPS(nohz_tick_exit, struct scx_exit_info *ei)
{
	UEI_RECORD(uei, ei);
}

SEC(".struct_ops.link")
struct sched_ext_ops nohz_tick_ops = {
	.select_cpu		= (void *)nohz_tick_select_cpu,
	.enqueue		= (void *)nohz_tick_enqueue,
	.running		= (void *)nohz_tick_running,
	.tick			= (void *)nohz_tick_tick,
	.exit			= (void *)nohz_tick_exit,
	.name			= "nohz_tick",
	.timeout_ms		= 1000U,
};
