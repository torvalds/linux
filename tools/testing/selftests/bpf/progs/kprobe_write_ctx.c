// SPDX-License-Identifier: GPL-2.0
#include "vmlinux.h"
#include <bpf/bpf_helpers.h>
#include <bpf/bpf_tracing.h>

char _license[] SEC("license") = "GPL";

#if defined(__TARGET_ARCH_x86)
SEC("kprobe")
int kprobe_write_ctx(struct pt_regs *ctx)
{
	ctx->ax = 0;
	return 0;
}

SEC("kprobe.multi")
int kprobe_multi_write_ctx(struct pt_regs *ctx)
{
	ctx->ax = 0;
	return 0;
}

SEC("?kprobe")
int kprobe_dummy(struct pt_regs *regs)
{
	return 0;
}

SEC("?freplace")
int freplace_kprobe(struct pt_regs *regs)
{
	regs->di = 0;
	return 0;
}

SEC("?fentry/bpf_fentry_test1")
int BPF_PROG(fentry)
{
	return 0;
}
#endif
