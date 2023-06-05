// SPDX-License-Identifier: GPL-2.0
// Copyright (c) 2017 Facebook

#include "vmlinux.h"
#include <bpf/bpf_helpers.h>
#include <bpf/bpf_tracing.h>
#include <bpf/bpf_core_read.h>
#include "bpf_misc.h"

int kprobe_res = 0;
int kretprobe_res = 0;
int uprobe_res = 0;
int uretprobe_res = 0;
int uprobe_byname_res = 0;
void *user_ptr = 0;

SEC("kprobe")
int handle_kprobe(struct pt_regs *ctx)
{
	kprobe_res = 1;
	return 0;
}

SEC("kretprobe")
int handle_kretprobe(struct pt_regs *ctx)
{
	kretprobe_res = 2;
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


char _license[] SEC("license") = "GPL";
