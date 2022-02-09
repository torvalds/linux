// SPDX-License-Identifier: GPL-2.0
/* Copyright 2022 Sony Group Corporation */
#include <vmlinux.h>

#include <bpf/bpf_core_read.h>
#include <bpf/bpf_helpers.h>
#include <bpf/bpf_tracing.h>
#include "bpf_misc.h"

int arg1 = 0;
unsigned long arg2 = 0;
unsigned long arg3 = 0;
unsigned long arg4_cx = 0;
unsigned long arg4 = 0;
unsigned long arg5 = 0;

int arg1_core = 0;
unsigned long arg2_core = 0;
unsigned long arg3_core = 0;
unsigned long arg4_core_cx = 0;
unsigned long arg4_core = 0;
unsigned long arg5_core = 0;

const volatile pid_t filter_pid = 0;

SEC("kprobe/" SYS_PREFIX "sys_prctl")
int BPF_KPROBE(handle_sys_prctl)
{
	struct pt_regs *real_regs;
	pid_t pid = bpf_get_current_pid_tgid() >> 32;
	unsigned long tmp;

	if (pid != filter_pid)
		return 0;

	real_regs = (struct pt_regs *)PT_REGS_PARM1(ctx);

	/* test for PT_REGS_PARM */

	bpf_probe_read_kernel(&tmp, sizeof(tmp), &PT_REGS_PARM1_SYSCALL(real_regs));
	arg1 = tmp;
	bpf_probe_read_kernel(&arg2, sizeof(arg2), &PT_REGS_PARM2_SYSCALL(real_regs));
	bpf_probe_read_kernel(&arg3, sizeof(arg3), &PT_REGS_PARM3_SYSCALL(real_regs));
	bpf_probe_read_kernel(&arg4_cx, sizeof(arg4_cx), &PT_REGS_PARM4(real_regs));
	bpf_probe_read_kernel(&arg4, sizeof(arg4), &PT_REGS_PARM4_SYSCALL(real_regs));
	bpf_probe_read_kernel(&arg5, sizeof(arg5), &PT_REGS_PARM5_SYSCALL(real_regs));

	/* test for the CORE variant of PT_REGS_PARM */
	arg1_core = PT_REGS_PARM1_CORE_SYSCALL(real_regs);
	arg2_core = PT_REGS_PARM2_CORE_SYSCALL(real_regs);
	arg3_core = PT_REGS_PARM3_CORE_SYSCALL(real_regs);
	arg4_core_cx = PT_REGS_PARM4_CORE(real_regs);
	arg4_core = PT_REGS_PARM4_CORE_SYSCALL(real_regs);
	arg5_core = PT_REGS_PARM5_CORE_SYSCALL(real_regs);

	return 0;
}

char _license[] SEC("license") = "GPL";
