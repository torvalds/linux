// SPDX-License-Identifier: GPL-2.0
/*
 * A scheduler that verified if RT tasks can stall SCHED_EXT tasks.
 *
 * Copyright (c) 2025 NVIDIA Corporation.
 */

#include <scx/common.bpf.h>

char _license[] SEC("license") = "GPL";

UEI_DEFINE(uei);

void BPF_STRUCT_OPS(rt_stall_exit, struct scx_exit_info *ei)
{
	UEI_RECORD(uei, ei);
}

SEC(".struct_ops.link")
struct sched_ext_ops rt_stall_ops = {
	.exit			= (void *)rt_stall_exit,
	.name			= "rt_stall",
};
