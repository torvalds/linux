// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2019 Facebook */
#include <linux/bpf.h>
#include "bpf_helpers.h"
#include "bpf_tracing.h"
#include "bpf_trace_helpers.h"

SEC("kprobe/__set_task_comm")
int prog1(struct pt_regs *ctx)
{
	return 0;
}

SEC("kretprobe/__set_task_comm")
int prog2(struct pt_regs *ctx)
{
	return 0;
}

SEC("raw_tp/task_rename")
int prog3(struct bpf_raw_tracepoint_args *ctx)
{
	return 0;
}

struct task_struct;
BPF_TRACE_3("fentry/__set_task_comm", prog4,
	    struct task_struct *, tsk, const char *, buf, __u8, exec)
{
	return 0;
}

BPF_TRACE_3("fexit/__set_task_comm", prog5,
	    struct task_struct *, tsk, const char *, buf, __u8, exec)
{
	return 0;
}

char _license[] SEC("license") = "GPL";
