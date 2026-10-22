/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Create and destroy DSQs in a loop.
 *
 * Copyright (c) 2024 Meta Platforms, Inc. and affiliates.
 * Copyright (c) 2024 David Vernet <dvernet@meta.com>
 */

#include <scx/common.bpf.h>

char _license[] SEC("license") = "GPL";

u32 nr_lifecycle_tests;

UEI_DEFINE(uei);

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
		err = scx_bpf_create_dsq(i, -1);
		if (err != -EEXIST) {
			scx_bpf_error("Duplicate DSQ %d creation returned %d", i, err);
			return -EINVAL;
		}

		/* A rejected duplicate must leave the original DSQ accessible. */
		err = scx_bpf_dsq_nr_queued(i);
		if (err) {
			scx_bpf_error("Original DSQ %d queue count is %d", i, err);
			return -EINVAL;
		}

		scx_bpf_destroy_dsq(i);
		err = scx_bpf_dsq_nr_queued(i);
		if (err != -ENOENT) {
			scx_bpf_error("Destroyed DSQ %d queue count is %d", i, err);
			return -EINVAL;
		}

		err = scx_bpf_create_dsq(i, -1);
		if (err) {
			scx_bpf_error("Failed to recreate DSQ %d: %d", i, err);
			return err;
		}

		err = scx_bpf_dsq_nr_queued(i);
		if (err) {
			scx_bpf_error("Recreated DSQ %d queue count is %d", i, err);
			return -EINVAL;
		}

		scx_bpf_destroy_dsq(i);
		nr_lifecycle_tests++;
	}

	return 0;
}

void BPF_STRUCT_OPS(create_dsq_exit, struct scx_exit_info *ei)
{
	UEI_RECORD(uei, ei);
}

SEC(".struct_ops.link")
struct sched_ext_ops create_dsq_ops = {
	.init_task		= (void *) create_dsq_init_task,
	.exit_task		= (void *) create_dsq_exit_task,
	.init			= (void *) create_dsq_init,
	.exit			= (void *) create_dsq_exit,
	.name			= "create_dsq",
};
