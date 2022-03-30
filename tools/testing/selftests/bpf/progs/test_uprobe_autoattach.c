// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2022, Oracle and/or its affiliates. */

#include <linux/ptrace.h>
#include <linux/bpf.h>
#include <bpf/bpf_helpers.h>
#include <bpf/bpf_tracing.h>

int uprobe_byname_res = 0;
int uretprobe_byname_res = 0;
int uprobe_byname2_res = 0;
int uretprobe_byname2_res = 0;

/* This program cannot auto-attach, but that should not stop other
 * programs from attaching.
 */
SEC("uprobe")
int handle_uprobe_noautoattach(struct pt_regs *ctx)
{
	return 0;
}

SEC("uprobe//proc/self/exe:autoattach_trigger_func")
int handle_uprobe_byname(struct pt_regs *ctx)
{
	uprobe_byname_res = 1;
	return 0;
}

SEC("uretprobe//proc/self/exe:autoattach_trigger_func")
int handle_uretprobe_byname(struct pt_regs *ctx)
{
	uretprobe_byname_res = 2;
	return 0;
}


SEC("uprobe/libc.so.6:malloc")
int handle_uprobe_byname2(struct pt_regs *ctx)
{
	uprobe_byname2_res = 3;
	return 0;
}

SEC("uretprobe/libc.so.6:free")
int handle_uretprobe_byname2(struct pt_regs *ctx)
{
	uretprobe_byname2_res = 4;
	return 0;
}

char _license[] SEC("license") = "GPL";
