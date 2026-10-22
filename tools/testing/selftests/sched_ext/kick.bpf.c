// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES
 */
#include <scx/common.bpf.h>

#include "kick_test.h"

char _license[] SEC("license") = "GPL";

const volatile u32 scenario;
const volatile s32 victim_pid;
const volatile s32 challenger_pid;
const volatile s32 target_cpu;
const volatile s32 lazy_resched_override = -1;

u32 state;
u64 slice_before;
u64 slice_at_resched;
s32 resched_tif;
u64 nr_wait_callbacks;
bool enq_preempt_issued;

UEI_DEFINE(uei);

static bool is_trace_scenario(void)
{
	return scenario <= TICK_EXPIRY;
}

void BPF_STRUCT_OPS(kick_enqueue, struct task_struct *p, u64 enq_flags)
{
	switch (scenario) {
	case ENQ_BOTH:
		if (p->pid == victim_pid) {
			scx_bpf_dsq_insert(p, SCX_DSQ_GLOBAL, SCX_SLICE_INF, enq_flags);
		} else if (p->pid == challenger_pid && state == KICK_STATE_QUEUED) {
			enq_preempt_issued = true;
			scx_bpf_dsq_insert(p, SCX_DSQ_LOCAL, SCX_SLICE_DFL,
					   enq_flags | SCX_ENQ_PREEMPT |
					   SCX_ENQ_PREEMPT_LAZY);
		} else {
			scx_bpf_dsq_insert(p, SCX_DSQ_GLOBAL, SCX_SLICE_DFL, enq_flags);
		}
		goto kick_last;
	case INVALID_KICK_IDLE:
		scx_bpf_kick_cpu(scx_bpf_task_cpu(p), SCX_KICK_PREEMPT_LAZY | SCX_KICK_IDLE);
		break;
	case INVALID_KICK_UNKNOWN:
		scx_bpf_kick_cpu(scx_bpf_task_cpu(p), 1LLU << 63);
		break;
	}

	scx_bpf_dsq_insert(p, SCX_DSQ_GLOBAL, SCX_SLICE_DFL, enq_flags);
kick_last:
	if (enq_flags & SCX_ENQ_LAST)
		scx_bpf_kick_cpu(target_cpu, SCX_KICK_IDLE);
}

static void set_lazy_resched_override(struct task_struct *p)
{
	if (p->pid == victim_pid && lazy_resched_override >= 0)
		scx_bpf_task_set_lazy_resched(p, lazy_resched_override);
}

void BPF_STRUCT_OPS(kick_running, struct task_struct *p)
{
	if (!is_trace_scenario() || p->pid != victim_pid)
		return;
	set_lazy_resched_override(p);
	if (__sync_val_compare_and_swap(&state, KICK_STATE_ARMED, KICK_STATE_QUEUED) !=
	    KICK_STATE_ARMED)
		return;

	slice_before = p->scx.slice;

	switch (scenario) {
	case KICK_IMMEDIATE:
		scx_bpf_kick_cpu(target_cpu, SCX_KICK_PREEMPT);
		break;
	case KICK_LAZY:
		scx_bpf_kick_cpu(target_cpu, SCX_KICK_PREEMPT_LAZY);
		break;
	case KICK_LAZY_THEN_IMMEDIATE:
		scx_bpf_kick_cpu(target_cpu, SCX_KICK_PREEMPT_LAZY);
		scx_bpf_kick_cpu(target_cpu, SCX_KICK_PREEMPT);
		break;
	case KICK_IMMEDIATE_THEN_LAZY:
		scx_bpf_kick_cpu(target_cpu, SCX_KICK_PREEMPT);
		scx_bpf_kick_cpu(target_cpu, SCX_KICK_PREEMPT_LAZY);
		break;
	case KICK_PLAIN_THEN_LAZY:
		scx_bpf_kick_cpu(target_cpu, 0);
		scx_bpf_kick_cpu(target_cpu, SCX_KICK_PREEMPT_LAZY);
		break;
	case KICK_LAZY_THEN_PLAIN:
		scx_bpf_kick_cpu(target_cpu, SCX_KICK_PREEMPT_LAZY);
		scx_bpf_kick_cpu(target_cpu, 0);
		break;
	case KICK_BOTH:
		scx_bpf_kick_cpu(target_cpu, SCX_KICK_PREEMPT | SCX_KICK_PREEMPT_LAZY);
		break;
	case KICK_LAZY_WAIT:
		scx_bpf_kick_cpu(target_cpu, SCX_KICK_PREEMPT_LAZY | SCX_KICK_WAIT);
		break;
	case ENQ_BOTH:
		/* The userspace controller wakes a competing task. */
		break;
	case TICK_EXPIRY:
		/* no kick: the slice runs out at the tick */
		break;
	}
}

SEC("fexit/__resched_curr")
int BPF_PROG(kick_need_resched, struct rq *rq, int tif)
{
	struct task_struct *task = BPF_CORE_READ(rq, curr);
	u64 slice;

	if (!task || BPF_CORE_READ(task, pid) != victim_pid ||
	    BPF_CORE_READ(rq, cpu) != target_cpu || state != KICK_STATE_QUEUED)
		return 0;
	if (scenario == ENQ_BOTH && !enq_preempt_issued)
		return 0;

	slice = BPF_CORE_READ(task, scx.slice);
	if (slice)
		return 0;

	slice_at_resched = slice;
	resched_tif = tif;
	state = KICK_STATE_RESCHED;
	return 0;
}

SEC("fentry/kick_sync_wait_bal_cb")
int BPF_PROG(kick_wait_callback, struct rq *rq)
{
	if (scenario == KICK_LAZY_WAIT && BPF_CORE_READ(rq, cpu) == target_cpu)
		__sync_fetch_and_add(&nr_wait_callbacks, 1);
	return 0;
}

void BPF_STRUCT_OPS(kick_stopping, struct task_struct *p, bool runnable)
{
	if (p->pid == victim_pid && state == KICK_STATE_RESCHED)
		state = KICK_STATE_DONE;
}

void BPF_STRUCT_OPS(kick_exit, struct scx_exit_info *ei)
{
	UEI_RECORD(uei, ei);
}

SEC(".struct_ops.link")
struct sched_ext_ops kick_ops = {
	.enqueue		= (void *)kick_enqueue,
	.running		= (void *)kick_running,
	.stopping		= (void *)kick_stopping,
	.exit			= (void *)kick_exit,
	.flags			= SCX_OPS_ENQ_LAST,
	.name			= "kick",
};
