// SPDX-License-Identifier: GPL-2.0
// Copyright (c) 2017 Facebook

#include <linux/ptrace.h>
#include <linux/bpf.h>
#include <bpf/bpf_helpers.h>
#include <bpf/bpf_tracing.h>
#include "bpf_misc.h"

int kprobe_res = 0;
int kprobe2_res = 0;
int kretprobe_res = 0;
int kretprobe2_res = 0;
int uprobe_res = 0;
int uretprobe_res = 0;
int uprobe_byname_res = 0;
int uretprobe_byname_res = 0;
int uprobe_byname2_res = 0;
int uretprobe_byname2_res = 0;

SEC("kprobe")
int handle_kprobe(struct pt_regs *ctx)
{
	kprobe_res = 1;
	return 0;
}

SEC("kprobe/" SYS_PREFIX "sys_nanosleep")
int BPF_KPROBE(handle_kprobe_auto)
{
	kprobe2_res = 11;
	return 0;
}

SEC("kretprobe")
int handle_kretprobe(struct pt_regs *ctx)
{
	kretprobe_res = 2;
	return 0;
}

SEC("kretprobe/" SYS_PREFIX "sys_nanosleep")
int BPF_KRETPROBE(handle_kretprobe_auto)
{
	kretprobe2_res = 22;
	return 0;
}

SEC("uprobe")
int handle_uprobe(struct pt_regs *ctx)
{
	uprobe_res = 3;
	return 0;
}

SEC("uretprobe")
int handle_uretprobe(struct pt_regs *ctx)
{
	uretprobe_res = 4;
	return 0;
}

SEC("uprobe")
int handle_uprobe_byname(struct pt_regs *ctx)
{
	uprobe_byname_res = 5;
	return 0;
}

/* use auto-attach format for section definition. */
SEC("uretprobe//proc/self/exe:trigger_func2")
int handle_uretprobe_byname(struct pt_regs *ctx)
{
	uretprobe_byname_res = 6;
	return 0;
}

SEC("uprobe")
int handle_uprobe_byname2(struct pt_regs *ctx)
{
	unsigned int size = PT_REGS_PARM1(ctx);

	/* verify malloc size */
	if (size == 1)
		uprobe_byname2_res = 7;
	return 0;
}

SEC("uretprobe")
int handle_uretprobe_byname2(struct pt_regs *ctx)
{
	uretprobe_byname2_res = 8;
	return 0;
}

char _license[] SEC("license") = "GPL";
