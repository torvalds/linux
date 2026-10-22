// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES
 *
 * Verify that SCX_OPS_ENQ_BLOCKED passes blocked proxy donors through
 * ops.enqueue() and record whether callbacks occur on the donor or owner CPU.
 */

#include <scx/common.bpf.h>

#define SHARED_DSQ 0

char _license[] SEC("license") = "GPL";

s32 donor_pid;
s32 donor_cpu = -1;
s32 owner_cpu = -1;
u64 nr_blocked_enqueues;
u64 nr_blocked_enqueues_donor_cpu;
u64 nr_blocked_enqueues_owner_cpu;
u64 nr_blocked_enqueues_other_cpu;
u64 nr_blocked_wakeups;
static u64 vtime_now;

UEI_DEFINE(uei);

s32 BPF_STRUCT_OPS(enq_blocked_select_cpu,
		   struct task_struct *p, s32 prev_cpu, u64 wake_flags)
{
	return prev_cpu;
}

void BPF_STRUCT_OPS(enq_blocked_enqueue, struct task_struct *p, u64 enq_flags)
{
	u64 vtime = p->scx.dsq_vtime;

	if (enq_flags & SCX_ENQ_BLOCKED) {
		int cpu = scx_bpf_task_cpu(p);

		if (enq_flags & SCX_ENQ_WAKEUP)
			__sync_fetch_and_add(&nr_blocked_wakeups, 1);

		if (p->pid == donor_pid) {
			__sync_fetch_and_add(&nr_blocked_enqueues, 1);
			if (cpu == donor_cpu)
				__sync_fetch_and_add(&nr_blocked_enqueues_donor_cpu, 1);
			else if (cpu == owner_cpu)
				__sync_fetch_and_add(&nr_blocked_enqueues_owner_cpu, 1);
			else
				__sync_fetch_and_add(&nr_blocked_enqueues_other_cpu, 1);
		}
	}

	/* Limit the amount of budget an idling task can accumulate. */
	if (time_before(vtime, vtime_now - SCX_SLICE_DFL))
		vtime = vtime_now - SCX_SLICE_DFL;

	scx_bpf_dsq_insert_vtime(p, SHARED_DSQ, SCX_SLICE_DFL, vtime,
				 enq_flags);
	scx_bpf_kick_cpu(scx_bpf_task_cpu(p), SCX_KICK_IDLE);
}

void BPF_STRUCT_OPS(enq_blocked_dispatch, s32 cpu, struct task_struct *prev)
{
	scx_bpf_dsq_move_to_local(SHARED_DSQ, 0);
}

void BPF_STRUCT_OPS(enq_blocked_running, struct task_struct *p)
{
	if (time_before(vtime_now, p->scx.dsq_vtime))
		vtime_now = p->scx.dsq_vtime;
}

void BPF_STRUCT_OPS(enq_blocked_stopping, struct task_struct *p, bool runnable)
{
	u64 delta = scale_by_task_weight_inverse(p,
					 SCX_SLICE_DFL - p->scx.slice);

	scx_bpf_task_set_dsq_vtime(p, p->scx.dsq_vtime + delta);
}

void BPF_STRUCT_OPS(enq_blocked_enable, struct task_struct *p)
{
	scx_bpf_task_set_dsq_vtime(p, vtime_now);
}

s32 BPF_STRUCT_OPS_SLEEPABLE(enq_blocked_init)
{
	int ret;

	ret = scx_bpf_create_dsq(SHARED_DSQ, -1);
	if (ret) {
		scx_bpf_error("failed to create DSQ %d (%d)", SHARED_DSQ, ret);
		return ret;
	}

	return 0;
}

void BPF_STRUCT_OPS(enq_blocked_exit, struct scx_exit_info *ei)
{
	UEI_RECORD(uei, ei);
}

SEC(".struct_ops.link")
struct sched_ext_ops enq_blocked_ops = {
	.select_cpu		= (void *)enq_blocked_select_cpu,
	.enqueue		= (void *)enq_blocked_enqueue,
	.dispatch		= (void *)enq_blocked_dispatch,
	.running		= (void *)enq_blocked_running,
	.stopping		= (void *)enq_blocked_stopping,
	.enable			= (void *)enq_blocked_enable,
	.init			= (void *)enq_blocked_init,
	.exit			= (void *)enq_blocked_exit,
	.name			= "enq_blocked",
};
