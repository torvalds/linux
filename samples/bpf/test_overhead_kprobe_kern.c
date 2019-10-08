/* Copyright (c) 2016 Facebook
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of version 2 of the GNU General Public
 * License as published by the Free Software Foundation.
 */
#include <linux/version.h>
#include <linux/ptrace.h>
#include <uapi/linux/bpf.h>
#include "bpf_helpers.h"
#include "bpf_tracing.h"

#define _(P) ({typeof(P) val = 0; bpf_probe_read(&val, sizeof(val), &P); val;})

SEC("kprobe/__set_task_comm")
int prog(struct pt_regs *ctx)
{
	struct signal_struct *signal;
	struct task_struct *tsk;
	char oldcomm[16] = {};
	char newcomm[16] = {};
	u16 oom_score_adj;
	u32 pid;

	tsk = (void *)PT_REGS_PARM1(ctx);

	pid = _(tsk->pid);
	bpf_probe_read(oldcomm, sizeof(oldcomm), &tsk->comm);
	bpf_probe_read(newcomm, sizeof(newcomm), (void *)PT_REGS_PARM2(ctx));
	signal = _(tsk->signal);
	oom_score_adj = _(signal->oom_score_adj);
	return 0;
}

SEC("kprobe/urandom_read")
int prog2(struct pt_regs *ctx)
{
	return 0;
}

char _license[] SEC("license") = "GPL";
u32 _version SEC("version") = LINUX_VERSION_CODE;
