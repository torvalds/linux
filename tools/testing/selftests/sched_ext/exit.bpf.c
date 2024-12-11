/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (c) 2024 Meta Platforms, Inc. and affiliates.
 * Copyright (c) 2024 David Vernet <dvernet@meta.com>
 */

#include <scx/common.bpf.h>

char _license[] SEC("license") = "GPL";

#include "exit_test.h"

const volatile int exit_point;
UEI_DEFINE(uei);

#define EXIT_CLEANLY() scx_bpf_exit(exit_point, "%d", exit_point)

#define DSQ_ID 0

s32 BPF_STRUCT_OPS(exit_select_cpu, struct task_struct *p,
		   s32 prev_cpu, u64 wake_flags)
{
	bool found;

	if (exit_point == EXIT_SELECT_CPU)
		EXIT_CLEANLY();

	return scx_bpf_select_cpu_dfl(p, prev_cpu, wake_flags, &found);
}

void BPF_STRUCT_OPS(exit_enqueue, struct task_struct *p, u64 enq_flags)
{
	if (exit_point == EXIT_ENQUEUE)
		EXIT_CLEANLY();

	scx_bpf_dispatch(p, DSQ_ID, SCX_SLICE_DFL, enq_flags);
}

void BPF_STRUCT_OPS(exit_dispatch, s32 cpu, struct task_struct *p)
{
	if (exit_point == EXIT_DISPATCH)
		EXIT_CLEANLY();

	scx_bpf_consume(DSQ_ID);
}

void BPF_STRUCT_OPS(exit_enable, struct task_struct *p)
{
	if (exit_point == EXIT_ENABLE)
		EXIT_CLEANLY();
}

s32 BPF_STRUCT_OPS(exit_init_task, struct task_struct *p,
		    struct scx_init_task_args *args)
{
	if (exit_point == EXIT_INIT_TASK)
		EXIT_CLEANLY();

	return 0;
}

void BPF_STRUCT_OPS(exit_exit, struct scx_exit_info *ei)
{
	UEI_RECORD(uei, ei);
}

s32 BPF_STRUCT_OPS_SLEEPABLE(exit_init)
{
	if (exit_point == EXIT_INIT)
		EXIT_CLEANLY();

	return scx_bpf_create_dsq(DSQ_ID, -1);
}

SEC(".struct_ops.link")
struct sched_ext_ops exit_ops = {
	.select_cpu		= (void *) exit_select_cpu,
	.enqueue		= (void *) exit_enqueue,
	.dispatch		= (void *) exit_dispatch,
	.init_task		= (void *) exit_init_task,
	.enable			= (void *) exit_enable,
	.exit			= (void *) exit_exit,
	.init			= (void *) exit_init,
	.name			= "exit",
	.timeout_ms		= 1000U,
};
