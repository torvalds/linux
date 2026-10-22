// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES
 *
 * Exercise tick dependency transitions between infinite and finite slices.
 */
#include <scx/common.bpf.h>

#include "nohz_tick_test.h"

char _license[] SEC("license") = "GPL";

const volatile s32 test_cpu;
u32 phase;
s32 victim_pid;
s32 challenger_pid;
s32 trigger_pid;
u64 nr_inf_running;
u64 nr_finite_running;
u64 nr_finite_ticks;
u64 nr_lazy_victim_running;
u64 nr_lazy_enq_running;
u64 nr_lazy_kick_running;
u64 nr_lazy_ticks;

UEI_DEFINE(uei);

s32 BPF_STRUCT_OPS(nohz_tick_select_cpu, struct task_struct *p, s32 prev_cpu,
		   u64 wake_flags)
{
	return prev_cpu;
}

void BPF_STRUCT_OPS(nohz_tick_enqueue, struct task_struct *p, u64 enq_flags)
{
	u64 slice;
	u64 dsq_id = SCX_DSQ_GLOBAL;

	switch (phase) {
	case NOHZ_PHASE_INF:
		slice = SCX_SLICE_INF;
		break;
	case NOHZ_PHASE_FINITE:
		slice = 1000000ULL;
		break;
	case NOHZ_PHASE_LAZY_ENQ:
	case NOHZ_PHASE_LAZY_KICK:
		dsq_id = SCX_DSQ_LOCAL;
		slice = p->pid == victim_pid ? SCX_SLICE_INF : SCX_SLICE_DFL;
		if (phase == NOHZ_PHASE_LAZY_ENQ && p->pid == challenger_pid)
			enq_flags |= SCX_ENQ_PREEMPT_LAZY;
		break;
	default:
		slice = SCX_SLICE_DFL;
		break;
	}

	scx_bpf_dsq_insert(p, dsq_id, slice, enq_flags);
	if (phase == NOHZ_PHASE_LAZY_KICK && p->pid == trigger_pid)
		scx_bpf_kick_cpu(test_cpu, SCX_KICK_PREEMPT_LAZY);
	if (enq_flags & SCX_ENQ_LAST)
		scx_bpf_kick_cpu(test_cpu, SCX_KICK_IDLE);
}

void BPF_STRUCT_OPS(nohz_tick_running, struct task_struct *p)
{
	if (bpf_get_smp_processor_id() != test_cpu)
		return;

	if (phase == NOHZ_PHASE_FINITE)
		__sync_fetch_and_add(&nr_finite_running, 1);
	else if (phase == NOHZ_PHASE_INF)
		__sync_fetch_and_add(&nr_inf_running, 1);
	else if (p->pid == victim_pid)
		__sync_fetch_and_add(&nr_lazy_victim_running, 1);
	else if (phase == NOHZ_PHASE_LAZY_ENQ && p->pid == challenger_pid)
		__sync_fetch_and_add(&nr_lazy_enq_running, 1);
	else if (phase == NOHZ_PHASE_LAZY_KICK && p->pid == challenger_pid)
		__sync_fetch_and_add(&nr_lazy_kick_running, 1);
}

void BPF_STRUCT_OPS(nohz_tick_tick, struct task_struct *p)
{
	if (bpf_get_smp_processor_id() != test_cpu)
		return;

	if (phase == NOHZ_PHASE_FINITE)
		__sync_fetch_and_add(&nr_finite_ticks, 1);
	else if ((phase == NOHZ_PHASE_LAZY_ENQ ||
		  phase == NOHZ_PHASE_LAZY_KICK) && p->pid == victim_pid)
		__sync_fetch_and_add(&nr_lazy_ticks, 1);
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
	.timeout_ms		= 5000U,
};
