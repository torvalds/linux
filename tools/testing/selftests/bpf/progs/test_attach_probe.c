// SPDX-License-Identifier: GPL-2.0
// Copyright (c) 2017 Facebook

#include "vmlinux.h"
#include <bpf/bpf_helpers.h>
#include <bpf/bpf_tracing.h>
#include <bpf/bpf_core_read.h>
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
int uprobe_byname3_sleepable_res = 0;
int uprobe_byname3_res = 0;
int uretprobe_byname3_sleepable_res = 0;
int uretprobe_byname3_res = 0;
void *user_ptr = 0;

SEC("kprobe")
int handle_kprobe(struct pt_regs *ctx)
{
	kprobe_res = 1;
	return 0;
}

SEC("ksyscall/nanosleep")
int BPF_KSYSCALL(handle_kprobe_auto, struct __kernel_timespec *req, struct __kernel_timespec *rem)
{
	kprobe2_res = 11;
	return 0;
}

/**
 * This program will be manually made sleepable on the userspace side
 * and should thus be unattachable.
 */
SEC("kprobe/" SYS_PREFIX "sys_nanosleep")
int handle_kprobe_sleepable(struct pt_regs *ctx)
{
	kprobe_res = 2;
	return 0;
}

SEC("kretprobe")
int handle_kretprobe(struct pt_regs *ctx)
{
	kretprobe_res = 2;
	return 0;
}

SEC("kretsyscall/nanosleep")
int BPF_KRETPROBE(handle_kretprobe_auto, int ret)
{
	kretprobe2_res = 22;
	return ret;
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

static __always_inline bool verify_sleepable_user_copy(void)
{
	char data[9];

	bpf_copy_from_user(data, sizeof(data), user_ptr);
	return bpf_strncmp(data, sizeof(data), "test_data") == 0;
}

SEC("uprobe.s//proc/self/exe:trigger_func3")
int handle_uprobe_byname3_sleepable(struct pt_regs *ctx)
{
	if (verify_sleepable_user_copy())
		uprobe_byname3_sleepable_res = 9;
	return 0;
}

/**
 * same target as the uprobe.s above to force sleepable and non-sleepable
 * programs in the same bpf_prog_array
 */
SEC("uprobe//proc/self/exe:trigger_func3")
int handle_uprobe_byname3(struct pt_regs *ctx)
{
	uprobe_byname3_res = 10;
	return 0;
}

SEC("uretprobe.s//proc/self/exe:trigger_func3")
int handle_uretprobe_byname3_sleepable(struct pt_regs *ctx)
{
	if (verify_sleepable_user_copy())
		uretprobe_byname3_sleepable_res = 11;
	return 0;
}

SEC("uretprobe//proc/self/exe:trigger_func3")
int handle_uretprobe_byname3(struct pt_regs *ctx)
{
	uretprobe_byname3_res = 12;
	return 0;
}


char _license[] SEC("license") = "GPL";
