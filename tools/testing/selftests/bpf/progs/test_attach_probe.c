// SPDX-License-Identifier: GPL-2.0
// Copyright (c) 2017 Facebook

#include <linux/ptrace.h>
#include <linux/bpf.h>
#include <bpf/bpf_helpers.h>

int kprobe_res = 0;
int kretprobe_res = 0;
int uprobe_res = 0;
int uretprobe_res = 0;

SEC("kprobe/sys_nanosleep")
int handle_kprobe(struct pt_regs *ctx)
{
	kprobe_res = 1;
	return 0;
}

SEC("kretprobe/sys_nanosleep")
int handle_kretprobe(struct pt_regs *ctx)
{
	kretprobe_res = 2;
	return 0;
}

SEC("uprobe/trigger_func")
int handle_uprobe(struct pt_regs *ctx)
{
	uprobe_res = 3;
	return 0;
}

SEC("uretprobe/trigger_func")
int handle_uretprobe(struct pt_regs *ctx)
{
	uretprobe_res = 4;
	return 0;
}

char _license[] SEC("license") = "GPL";
