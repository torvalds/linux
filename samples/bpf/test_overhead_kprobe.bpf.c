/* Copyright (c) 2016 Facebook
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of version 2 of the GNU General Public
 * License as published by the Free Software Foundation.
 */
#include "vmlinux.h"
#include <linux/version.h>
#include <bpf/bpf_helpers.h>
#include <bpf/bpf_tracing.h>
#include <bpf/bpf_core_read.h>

SEC("kprobe/__set_task_comm")
int prog(struct pt_regs *ctx)
{
	struct signal_struct *signal;
	struct task_struct *tsk;
	char oldcomm[TASK_COMM_LEN] = {};
	char newcomm[TASK_COMM_LEN] = {};
	u16 oom_score_adj;
	u32 pid;

	tsk = (void *)PT_REGS_PARM1_CORE(ctx);

	pid = BPF_CORE_READ(tsk, pid);
	bpf_core_read_str(oldcomm, sizeof(oldcomm), &tsk->comm);
	bpf_core_read_str(newcomm, sizeof(newcomm),
				  (void *)PT_REGS_PARM2(ctx));
	signal = BPF_CORE_READ(tsk, signal);
	oom_score_adj = BPF_CORE_READ(signal, oom_score_adj);
	return 0;
}

SEC("kprobe/fib_table_lookup")
int prog2(struct pt_regs *ctx)
{
	return 0;
}

char _license[] SEC("license") = "GPL";
u32 _version SEC("version") = LINUX_VERSION_CODE;
