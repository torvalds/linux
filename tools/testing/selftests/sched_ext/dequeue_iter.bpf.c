// SPDX-License-Identifier: GPL-2.0
/*
 * ops.dequeue() of this scheduler iterates the user DSQ it consumes
 * tasks from with bpf_iter_scx_dsq, which takes the DSQ lock.
 * On a kernel that still runs ops.dequeue() with that lock held, the
 * iteration self-deadlocks the CPU - this test wedges the system on
 * unfixed kernels instead of failing cleanly.
 *
 * Copyright (c) 2026 fangqiurong <fangqiurong@kylinos.cn>
 */

#include <scx/common.bpf.h>

char _license[] SEC("license") = "GPL";

UEI_DEFINE(uei);

#define TEST_DSQ_ID 1000

u64 dq_count;

s32 BPF_STRUCT_OPS_SLEEPABLE(dequeue_iter_init)
{
	return scx_bpf_create_dsq(TEST_DSQ_ID, -1);
}

s32 BPF_STRUCT_OPS(dequeue_iter_select_cpu, struct task_struct *p,
		   s32 prev_cpu, u64 wake_flags)
{
	return prev_cpu;
}

void BPF_STRUCT_OPS(dequeue_iter_enqueue, struct task_struct *p, u64 enq_flags)
{
	scx_bpf_dsq_insert(p, TEST_DSQ_ID, SCX_SLICE_DFL, enq_flags);
}

void BPF_STRUCT_OPS(dequeue_iter_dispatch, s32 cpu, struct task_struct *task)
{
	scx_bpf_dsq_move_to_local(TEST_DSQ_ID, 0);
}

void BPF_STRUCT_OPS(dequeue_iter_dequeue, struct task_struct *p, u64 deq_flags)
{
	struct bpf_iter_scx_dsq it;
	struct task_struct *t;

	if (!bpf_iter_scx_dsq_new(&it, TEST_DSQ_ID, 0)) {
		while ((t = bpf_iter_scx_dsq_next(&it)))
			;
	}
	bpf_iter_scx_dsq_destroy(&it);

	__sync_fetch_and_add(&dq_count, 1);
}

void BPF_STRUCT_OPS(dequeue_iter_exit, struct scx_exit_info *ei)
{
	UEI_RECORD(uei, ei);
	scx_bpf_destroy_dsq(TEST_DSQ_ID);
}

SEC(".struct_ops.link")
struct sched_ext_ops dequeue_iter_ops = {
	.init			= (void *)dequeue_iter_init,
	.select_cpu		= (void *)dequeue_iter_select_cpu,
	.enqueue		= (void *)dequeue_iter_enqueue,
	.dispatch		= (void *)dequeue_iter_dispatch,
	.dequeue		= (void *)dequeue_iter_dequeue,
	.exit			= (void *)dequeue_iter_exit,
	.timeout_ms		= 1000U,
	.name			= "dequeue_iter",
};
