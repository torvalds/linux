// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2022, Oracle and/or its affiliates. */

#include "vmlinux.h"

#include <bpf/bpf_core_read.h>
#include <bpf/bpf_helpers.h>
#include <bpf/bpf_tracing.h>
#include "bpf_misc.h"

int uprobe_byname_parm1 = 0;
int uprobe_byname_ran = 0;
int uretprobe_byname_rc = 0;
int uretprobe_byname_ret = 0;
int uretprobe_byname_ran = 0;
u64 uprobe_byname2_parm1 = 0;
int uprobe_byname2_ran = 0;
u64 uretprobe_byname2_rc = 0;
int uretprobe_byname2_ran = 0;

int test_pid;

int a[8];

/* This program cannot auto-attach, but that should not stop other
 * programs from attaching.
 */
SEC("uprobe")
int handle_uprobe_noautoattach(struct pt_regs *ctx)
{
	return 0;
}

SEC("uprobe//proc/self/exe:autoattach_trigger_func")
int BPF_UPROBE(handle_uprobe_byname
	       , int arg1
	       , int arg2
	       , int arg3
#if FUNC_REG_ARG_CNT > 3
	       , int arg4
#endif
#if FUNC_REG_ARG_CNT > 4
	       , int arg5
#endif
#if FUNC_REG_ARG_CNT > 5
	       , int arg6
#endif
#if FUNC_REG_ARG_CNT > 6
	       , int arg7
#endif
#if FUNC_REG_ARG_CNT > 7
	       , int arg8
#endif
)
{
	uprobe_byname_parm1 = PT_REGS_PARM1_CORE(ctx);
	uprobe_byname_ran = 1;

	a[0] = arg1;
	a[1] = arg2;
	a[2] = arg3;
#if FUNC_REG_ARG_CNT > 3
	a[3] = arg4;
#endif
#if FUNC_REG_ARG_CNT > 4
	a[4] = arg5;
#endif
#if FUNC_REG_ARG_CNT > 5
	a[5] = arg6;
#endif
#if FUNC_REG_ARG_CNT > 6
	a[6] = arg7;
#endif
#if FUNC_REG_ARG_CNT > 7
	a[7] = arg8;
#endif
	return 0;
}

SEC("uretprobe//proc/self/exe:autoattach_trigger_func")
int BPF_URETPROBE(handle_uretprobe_byname, int ret)
{
	uretprobe_byname_rc = PT_REGS_RC_CORE(ctx);
	uretprobe_byname_ret = ret;
	uretprobe_byname_ran = 2;

	return 0;
}


SEC("uprobe/libc.so.6:fopen")
int BPF_UPROBE(handle_uprobe_byname2, const char *pathname, const char *mode)
{
	int pid = bpf_get_current_pid_tgid() >> 32;

	/* ignore irrelevant invocations */
	if (test_pid != pid)
		return 0;
	uprobe_byname2_parm1 = (u64)(long)pathname;
	uprobe_byname2_ran = 3;
	return 0;
}

SEC("uretprobe/libc.so.6:fopen")
int BPF_URETPROBE(handle_uretprobe_byname2, void *ret)
{
	int pid = bpf_get_current_pid_tgid() >> 32;

	/* ignore irrelevant invocations */
	if (test_pid != pid)
		return 0;
	uretprobe_byname2_rc = (u64)(long)ret;
	uretprobe_byname2_ran = 4;
	return 0;
}

char _license[] SEC("license") = "GPL";
