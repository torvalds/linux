/* SPDX-License-Identifier: GPL-2.0 */
/*
 * A scheduler that validates the behavior of direct dispatching with a default
 * select_cpu implementation, and with the SCX_OPS_ENQ_DFL_NO_DISPATCH ops flag
 * specified.
 *
 * Copyright (c) 2023 Meta Platforms, Inc. and affiliates.
 * Copyright (c) 2023 David Vernet <dvernet@meta.com>
 * Copyright (c) 2023 Tejun Heo <tj@kernel.org>
 */

#include <scx/common.bpf.h>

char _license[] SEC("license") = "GPL";

bool saw_local = false;

/* Per-task scheduling context */
struct task_ctx {
	bool	force_local;	/* CPU changed by ops.select_cpu() */
};

struct {
	__uint(type, BPF_MAP_TYPE_TASK_STORAGE);
	__uint(map_flags, BPF_F_NO_PREALLOC);
	__type(key, int);
	__type(value, struct task_ctx);
} task_ctx_stor SEC(".maps");

/* Manually specify the signature until the kfunc is added to the scx repo. */
s32 scx_bpf_select_cpu_dfl(struct task_struct *p, s32 prev_cpu, u64 wake_flags,
			   bool *found) __ksym;

s32 BPF_STRUCT_OPS(select_cpu_dfl_nodispatch_select_cpu, struct task_struct *p,
		   s32 prev_cpu, u64 wake_flags)
{
	struct task_ctx *tctx;
	s32 cpu;

	tctx = bpf_task_storage_get(&task_ctx_stor, p, 0, 0);
	if (!tctx) {
		scx_bpf_error("task_ctx lookup failed");
		return -ESRCH;
	}

	cpu = scx_bpf_select_cpu_dfl(p, prev_cpu, wake_flags,
				     &tctx->force_local);

	return cpu;
}

void BPF_STRUCT_OPS(select_cpu_dfl_nodispatch_enqueue, struct task_struct *p,
		    u64 enq_flags)
{
	u64 dsq_id = SCX_DSQ_GLOBAL;
	struct task_ctx *tctx;

	tctx = bpf_task_storage_get(&task_ctx_stor, p, 0, 0);
	if (!tctx) {
		scx_bpf_error("task_ctx lookup failed");
		return;
	}

	if (tctx->force_local) {
		dsq_id = SCX_DSQ_LOCAL;
		tctx->force_local = false;
		saw_local = true;
	}

	scx_bpf_dsq_insert(p, dsq_id, SCX_SLICE_DFL, enq_flags);
}

s32 BPF_STRUCT_OPS(select_cpu_dfl_nodispatch_init_task,
		   struct task_struct *p, struct scx_init_task_args *args)
{
	if (bpf_task_storage_get(&task_ctx_stor, p, 0,
				 BPF_LOCAL_STORAGE_GET_F_CREATE))
		return 0;
	else
		return -ENOMEM;
}

SEC(".struct_ops.link")
struct sched_ext_ops select_cpu_dfl_nodispatch_ops = {
	.select_cpu		= (void *) select_cpu_dfl_nodispatch_select_cpu,
	.enqueue		= (void *) select_cpu_dfl_nodispatch_enqueue,
	.init_task		= (void *) select_cpu_dfl_nodispatch_init_task,
	.name			= "select_cpu_dfl_nodispatch",
};
