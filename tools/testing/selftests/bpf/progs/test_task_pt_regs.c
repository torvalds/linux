// SPDX-License-Identifier: GPL-2.0

#include "vmlinux.h"
#include <bpf/bpf_helpers.h>
#include <bpf/bpf_tracing.h>

#define PT_REGS_SIZE sizeof(struct pt_regs)

/*
 * The kernel struct pt_regs isn't exported in its entirety to userspace.
 * Pass it as an array to task_pt_regs.c
 */
char current_regs[PT_REGS_SIZE] = {};
char ctx_regs[PT_REGS_SIZE] = {};
int uprobe_res = 0;

SEC("uprobe/trigger_func")
int handle_uprobe(struct pt_regs *ctx)
{
	struct task_struct *current;
	struct pt_regs *regs;

	current = bpf_get_current_task_btf();
	regs = (struct pt_regs *) bpf_task_pt_regs(current);
	if (bpf_probe_read_kernel(current_regs, PT_REGS_SIZE, regs))
		return 0;
	if (bpf_probe_read_kernel(ctx_regs, PT_REGS_SIZE, ctx))
		return 0;

	/* Prove that uprobe was run */
	uprobe_res = 1;

	return 0;
}

char _license[] SEC("license") = "GPL";
