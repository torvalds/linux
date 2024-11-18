/* SPDX-License-Identifier: GPL-2.0 */
/*
 * A scheduler that validates that we can invoke sched_ext kfuncs in
 * BPF_PROG_TYPE_SYSCALL programs.
 *
 * Copyright (c) 2024 Meta Platforms, Inc. and affiliates.
 * Copyright (c) 2024 David Vernet <dvernet@meta.com>
 */

#include <scx/common.bpf.h>

UEI_DEFINE(uei);

char _license[] SEC("license") = "GPL";

SEC("syscall")
int BPF_PROG(prog_run_syscall)
{
	scx_bpf_create_dsq(0, -1);
	scx_bpf_exit(0xdeadbeef, "Exited from PROG_RUN");
	return 0;
}

void BPF_STRUCT_OPS(prog_run_exit, struct scx_exit_info *ei)
{
	UEI_RECORD(uei, ei);
}

SEC(".struct_ops.link")
struct sched_ext_ops prog_run_ops = {
	.exit			= prog_run_exit,
	.name			= "prog_run",
};
