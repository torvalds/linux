// SPDX-License-Identifier: GPL-2.0
/*
 * Verify that the BPF verifier rejects scx_bpf_dsq_move() from ops.dump().
 *
 * Copyright (c) 2026 Google LLC.
 */

#include <scx/common.bpf.h>

#define SHARED_DSQ	0

char _license[] SEC("license") = "GPL";

void BPF_STRUCT_OPS(unlocked_kfunc_fail_dump_cb, struct scx_dump_ctx *dctx)
{
	struct task_struct *p;

	bpf_for_each(scx_dsq, p, SHARED_DSQ, 0)
		scx_bpf_dsq_move(BPF_FOR_EACH_ITER, p, SCX_DSQ_GLOBAL, 0);
}

SEC(".struct_ops.link")
struct sched_ext_ops unlocked_kfunc_fail_dump_ops = {
	.dump			= (void *)unlocked_kfunc_fail_dump_cb,
	.name			= "unlocked_kfunc_fail_dump",
};
