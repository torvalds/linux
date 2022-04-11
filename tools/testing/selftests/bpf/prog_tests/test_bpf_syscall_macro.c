// SPDX-License-Identifier: GPL-2.0
/* Copyright 2022 Sony Group Corporation */
#include <sys/prctl.h>
#include <test_progs.h>
#include "bpf_syscall_macro.skel.h"

void test_bpf_syscall_macro(void)
{
	struct bpf_syscall_macro *skel = NULL;
	int err;
	int exp_arg1 = 1001;
	unsigned long exp_arg2 = 12;
	unsigned long exp_arg3 = 13;
	unsigned long exp_arg4 = 14;
	unsigned long exp_arg5 = 15;

	/* check whether it can open program */
	skel = bpf_syscall_macro__open();
	if (!ASSERT_OK_PTR(skel, "bpf_syscall_macro__open"))
		return;

	skel->rodata->filter_pid = getpid();

	/* check whether it can load program */
	err = bpf_syscall_macro__load(skel);
	if (!ASSERT_OK(err, "bpf_syscall_macro__load"))
		goto cleanup;

	/* check whether it can attach kprobe */
	err = bpf_syscall_macro__attach(skel);
	if (!ASSERT_OK(err, "bpf_syscall_macro__attach"))
		goto cleanup;

	/* check whether args of syscall are copied correctly */
	prctl(exp_arg1, exp_arg2, exp_arg3, exp_arg4, exp_arg5);
#if defined(__aarch64__) || defined(__s390__)
	ASSERT_NEQ(skel->bss->arg1, exp_arg1, "syscall_arg1");
#else
	ASSERT_EQ(skel->bss->arg1, exp_arg1, "syscall_arg1");
#endif
	ASSERT_EQ(skel->bss->arg2, exp_arg2, "syscall_arg2");
	ASSERT_EQ(skel->bss->arg3, exp_arg3, "syscall_arg3");
	/* it cannot copy arg4 when uses PT_REGS_PARM4 on x86_64 */
#ifdef __x86_64__
	ASSERT_NEQ(skel->bss->arg4_cx, exp_arg4, "syscall_arg4_from_cx");
#else
	ASSERT_EQ(skel->bss->arg4_cx, exp_arg4, "syscall_arg4_from_cx");
#endif
	ASSERT_EQ(skel->bss->arg4, exp_arg4, "syscall_arg4");
	ASSERT_EQ(skel->bss->arg5, exp_arg5, "syscall_arg5");

	/* check whether args of syscall are copied correctly for CORE variants */
	ASSERT_EQ(skel->bss->arg1_core, exp_arg1, "syscall_arg1_core_variant");
	ASSERT_EQ(skel->bss->arg2_core, exp_arg2, "syscall_arg2_core_variant");
	ASSERT_EQ(skel->bss->arg3_core, exp_arg3, "syscall_arg3_core_variant");
	/* it cannot copy arg4 when uses PT_REGS_PARM4_CORE on x86_64 */
#ifdef __x86_64__
	ASSERT_NEQ(skel->bss->arg4_core_cx, exp_arg4, "syscall_arg4_from_cx_core_variant");
#else
	ASSERT_EQ(skel->bss->arg4_core_cx, exp_arg4, "syscall_arg4_from_cx_core_variant");
#endif
	ASSERT_EQ(skel->bss->arg4_core, exp_arg4, "syscall_arg4_core_variant");
	ASSERT_EQ(skel->bss->arg5_core, exp_arg5, "syscall_arg5_core_variant");

	ASSERT_EQ(skel->bss->option_syscall, exp_arg1, "BPF_KPROBE_SYSCALL_option");
	ASSERT_EQ(skel->bss->arg2_syscall, exp_arg2, "BPF_KPROBE_SYSCALL_arg2");
	ASSERT_EQ(skel->bss->arg3_syscall, exp_arg3, "BPF_KPROBE_SYSCALL_arg3");
	ASSERT_EQ(skel->bss->arg4_syscall, exp_arg4, "BPF_KPROBE_SYSCALL_arg4");
	ASSERT_EQ(skel->bss->arg5_syscall, exp_arg5, "BPF_KPROBE_SYSCALL_arg5");

cleanup:
	bpf_syscall_macro__destroy(skel);
}
