// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2021 Facebook */

#include "vmlinux.h"
#include <bpf/bpf_helpers.h>
#include <bpf/bpf_tracing.h>

int my_tid;

int kprobe_res;
int kprobe_multi_res;
int kretprobe_res;
int uprobe_res;
int uretprobe_res;
int tp_res;
int pe_res;

static void update(void *ctx, int *res)
{
	if (my_tid != (u32)bpf_get_current_pid_tgid())
		return;

	*res |= bpf_get_attach_cookie(ctx);
}

SEC("kprobe/sys_nanosleep")
int handle_kprobe(struct pt_regs *ctx)
{
	update(ctx, &kprobe_res);
	return 0;
}

SEC("kretprobe/sys_nanosleep")
int handle_kretprobe(struct pt_regs *ctx)
{
	update(ctx, &kretprobe_res);
	return 0;
}

SEC("uprobe")
int handle_uprobe(struct pt_regs *ctx)
{
	update(ctx, &uprobe_res);
	return 0;
}

SEC("uretprobe")
int handle_uretprobe(struct pt_regs *ctx)
{
	update(ctx, &uretprobe_res);
	return 0;
}

/* bpf_prog_array, used by kernel internally to keep track of attached BPF
 * programs to a given BPF hook (e.g., for tracepoints) doesn't allow the same
 * BPF program to be attached multiple times. So have three identical copies
 * ready to attach to the same tracepoint.
 */
SEC("tp/syscalls/sys_enter_nanosleep")
int handle_tp1(struct pt_regs *ctx)
{
	update(ctx, &tp_res);
	return 0;
}
SEC("tp/syscalls/sys_enter_nanosleep")
int handle_tp2(struct pt_regs *ctx)
{
	update(ctx, &tp_res);
	return 0;
}
SEC("tp/syscalls/sys_enter_nanosleep")
int handle_tp3(void *ctx)
{
	update(ctx, &tp_res);
	return 1;
}

SEC("perf_event")
int handle_pe(struct pt_regs *ctx)
{
	update(ctx, &pe_res);
	return 0;
}

char _license[] SEC("license") = "GPL";
