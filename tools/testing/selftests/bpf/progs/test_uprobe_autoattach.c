// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2022, Oracle and/or its affiliates. */

#include "vmlinux.h"

#include <bpf/bpf_core_read.h>
#include <bpf/bpf_helpers.h>
#include <bpf/bpf_tracing.h>

int uprobe_byname_parm1 = 0;
int uprobe_byname_ran = 0;
int uretprobe_byname_rc = 0;
int uretprobe_byname_ran = 0;
size_t uprobe_byname2_parm1 = 0;
int uprobe_byname2_ran = 0;
char *uretprobe_byname2_rc = NULL;
int uretprobe_byname2_ran = 0;

int test_pid;

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
	uprobe_byname_parm1 = PT_REGS_PARM1_CORE(ctx);
	uprobe_byname_ran = 1;
	return 0;
}

SEC("uretprobe//proc/self/exe:autoattach_trigger_func")
int handle_uretprobe_byname(struct pt_regs *ctx)
{
	uretprobe_byname_rc = PT_REGS_RC_CORE(ctx);
	uretprobe_byname_ran = 2;
	return 0;
}


SEC("uprobe/libc.so.6:malloc")
int handle_uprobe_byname2(struct pt_regs *ctx)
{
	int pid = bpf_get_current_pid_tgid() >> 32;

	/* ignore irrelevant invocations */
	if (test_pid != pid)
		return 0;
	uprobe_byname2_parm1 = PT_REGS_PARM1_CORE(ctx);
	uprobe_byname2_ran = 3;
	return 0;
}

SEC("uretprobe/libc.so.6:malloc")
int handle_uretprobe_byname2(struct pt_regs *ctx)
{
	int pid = bpf_get_current_pid_tgid() >> 32;

	/* ignore irrelevant invocations */
	if (test_pid != pid)
		return 0;
	uretprobe_byname2_rc = (char *)PT_REGS_RC_CORE(ctx);
	uretprobe_byname2_ran = 4;
	return 0;
}

char _license[] SEC("license") = "GPL";
