// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2019 Facebook */
#include <linux/bpf.h>
#include "bpf_helpers.h"
#include "bpf_tracing.h"

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

struct __set_task_comm_args {
	struct task_struct *tsk;
	const char *buf;
	ku8 exec;
};

SEC("fentry/__set_task_comm")
int prog4(struct __set_task_comm_args *ctx)
{
	return 0;
}

SEC("fexit/__set_task_comm")
int prog5(struct __set_task_comm_args *ctx)
{
	return 0;
}

char _license[] SEC("license") = "GPL";
