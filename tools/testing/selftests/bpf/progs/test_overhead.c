// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2019 Facebook */
#include "vmlinux.h"
#include <bpf/bpf_helpers.h>
#include <bpf/bpf_tracing.h>

struct task_struct;

SEC("kprobe/__set_task_comm")
int BPF_KPROBE(prog1, struct task_struct *tsk, const char *buf, bool exec)
{
	return !tsk;
}

SEC("kretprobe/__set_task_comm")
int BPF_KRETPROBE(prog2, int ret)
{
	return ret;
}

SEC("raw_tp/task_rename")
int prog3(struct bpf_raw_tracepoint_args *ctx)
{
	return !ctx->args[0];
}

SEC("fentry/__set_task_comm")
int BPF_PROG(prog4, struct task_struct *tsk, const char *buf, bool exec)
{
	return 0;
}

SEC("fexit/__set_task_comm")
int BPF_PROG(prog5, struct task_struct *tsk, const char *buf, bool exec)
{
	return 0;
}

char _license[] SEC("license") = "GPL";
