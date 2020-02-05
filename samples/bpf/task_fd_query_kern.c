// SPDX-License-Identifier: GPL-2.0
#include <linux/version.h>
#include <linux/ptrace.h>
#include <uapi/linux/bpf.h>
#include <bpf/bpf_helpers.h>

SEC("kprobe/blk_mq_start_request")
int bpf_prog1(struct pt_regs *ctx)
{
	return 0;
}

SEC("kretprobe/blk_account_io_completion")
int bpf_prog2(struct pt_regs *ctx)
{
	return 0;
}
char _license[] SEC("license") = "GPL";
u32 _version SEC("version") = LINUX_VERSION_CODE;
