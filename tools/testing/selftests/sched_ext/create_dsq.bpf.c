/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Create and destroy DSQs in a loop.
 *
 * Copyright (c) 2024 Meta Platforms, Inc. and affiliates.
 * Copyright (c) 2024 David Vernet <dvernet@meta.com>
 */

#include <scx/common.bpf.h>

char _license[] SEC("license") = "GPL";

void BPF_STRUCT_OPS(create_dsq_exit_task, struct task_struct *p,
		    struct scx_exit_task_args *args)
{
	scx_bpf_destroy_dsq(p->pid);
}

s32 BPF_STRUCT_OPS_SLEEPABLE(create_dsq_init_task, struct task_struct *p,
			     struct scx_init_task_args *args)
{
	s32 err;

	err = scx_bpf_create_dsq(p->pid, -1);
	if (err)
		scx_bpf_error("Failed to create DSQ for %s[%d]",
			      p->comm, p->pid);

	return err;
}

s32 BPF_STRUCT_OPS_SLEEPABLE(create_dsq_init)
{
	u32 i;
	s32 err;

	bpf_for(i, 0, 1024) {
		err = scx_bpf_create_dsq(i, -1);
		if (err) {
			scx_bpf_error("Failed to create DSQ %d", i);
			return 0;
		}
	}

	bpf_for(i, 0, 1024) {
		scx_bpf_destroy_dsq(i);
	}

	return 0;
}

SEC(".struct_ops.link")
struct sched_ext_ops create_dsq_ops = {
	.init_task		= create_dsq_init_task,
	.exit_task		= create_dsq_exit_task,
	.init			= create_dsq_init,
	.name			= "create_dsq",
};
