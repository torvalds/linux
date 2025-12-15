/* SPDX-License-Identifier: GPL-2.0 */
/*
 * A CPU0 scheduler.
 *
 * This scheduler queues all tasks to a shared DSQ and only dispatches them on
 * CPU0 in FIFO order. This is useful for testing bypass behavior when many
 * tasks are concentrated on a single CPU. If the load balancer doesn't work,
 * bypass mode can trigger task hangs or RCU stalls as the queue is long and
 * there's only one CPU working on it.
 *
 * - Statistics tracking how many tasks are queued to local and CPU0 DSQs.
 * - Termination notification for userspace.
 *
 * Copyright (c) 2025 Meta Platforms, Inc. and affiliates.
 * Copyright (c) 2025 Tejun Heo <tj@kernel.org>
 */
#include <scx/common.bpf.h>

char _license[] SEC("license") = "GPL";

const volatile u32 nr_cpus = 32;	/* !0 for veristat, set during init */

UEI_DEFINE(uei);

/*
 * We create a custom DSQ with ID 0 that we dispatch to and consume from on
 * CPU0.
 */
#define DSQ_CPU0 0

struct {
	__uint(type, BPF_MAP_TYPE_PERCPU_ARRAY);
	__uint(key_size, sizeof(u32));
	__uint(value_size, sizeof(u64));
	__uint(max_entries, 2);			/* [local, cpu0] */
} stats SEC(".maps");

static void stat_inc(u32 idx)
{
	u64 *cnt_p = bpf_map_lookup_elem(&stats, &idx);
	if (cnt_p)
		(*cnt_p)++;
}

s32 BPF_STRUCT_OPS(cpu0_select_cpu, struct task_struct *p, s32 prev_cpu, u64 wake_flags)
{
	return 0;
}

void BPF_STRUCT_OPS(cpu0_enqueue, struct task_struct *p, u64 enq_flags)
{
	/*
	 * select_cpu() always picks CPU0. If @p is not on CPU0, it can't run on
	 * CPU 0. Queue on whichever CPU it's currently only.
	 */
	if (scx_bpf_task_cpu(p) != 0) {
		stat_inc(0);	/* count local queueing */
		scx_bpf_dsq_insert(p, SCX_DSQ_LOCAL, SCX_SLICE_DFL, 0);
		return;
	}

	stat_inc(1);	/* count cpu0 queueing */
	scx_bpf_dsq_insert(p, DSQ_CPU0, SCX_SLICE_DFL, enq_flags);
}

void BPF_STRUCT_OPS(cpu0_dispatch, s32 cpu, struct task_struct *prev)
{
	if (cpu == 0)
		scx_bpf_dsq_move_to_local(DSQ_CPU0);
}

s32 BPF_STRUCT_OPS_SLEEPABLE(cpu0_init)
{
	return scx_bpf_create_dsq(DSQ_CPU0, -1);
}

void BPF_STRUCT_OPS(cpu0_exit, struct scx_exit_info *ei)
{
	UEI_RECORD(uei, ei);
}

SCX_OPS_DEFINE(cpu0_ops,
	       .select_cpu		= (void *)cpu0_select_cpu,
	       .enqueue			= (void *)cpu0_enqueue,
	       .dispatch		= (void *)cpu0_dispatch,
	       .init			= (void *)cpu0_init,
	       .exit			= (void *)cpu0_exit,
	       .name			= "cpu0");
