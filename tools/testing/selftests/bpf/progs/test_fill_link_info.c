// SPDX-License-Identifier: GPL-2.0
/* Copyright (C) 2023 Yafang Shao <laoar.shao@gmail.com> */

#include "vmlinux.h"
#include <bpf/bpf_tracing.h>
#include <stdbool.h>

extern bool CONFIG_X86_KERNEL_IBT __kconfig __weak;

/* This function is here to have CONFIG_X86_KERNEL_IBT
 * used and added to object BTF.
 */
int unused(void)
{
	return CONFIG_X86_KERNEL_IBT ? 0 : 1;
}

SEC("kprobe")
int BPF_PROG(kprobe_run)
{
	return 0;
}

SEC("uprobe")
int BPF_PROG(uprobe_run)
{
	return 0;
}

SEC("tracepoint")
int BPF_PROG(tp_run)
{
	return 0;
}

SEC("kprobe.multi")
int BPF_PROG(kmulti_run)
{
	return 0;
}

SEC("uprobe.multi")
int BPF_PROG(umulti_run)
{
	return 0;
}

char _license[] SEC("license") = "GPL";
