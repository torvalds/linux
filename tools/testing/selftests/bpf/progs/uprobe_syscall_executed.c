// SPDX-License-Identifier: GPL-2.0
#include "vmlinux.h"
#include <bpf/bpf_helpers.h>
#include <bpf/bpf_tracing.h>
#include <bpf/usdt.bpf.h>
#include <string.h>

struct pt_regs regs;

char _license[] SEC("license") = "GPL";

int executed = 0;
int pid;

SEC("uprobe")
int BPF_UPROBE(test_uprobe)
{
	if (bpf_get_current_pid_tgid() >> 32 != pid)
		return 0;

	executed++;
	return 0;
}

SEC("uretprobe")
int BPF_URETPROBE(test_uretprobe)
{
	if (bpf_get_current_pid_tgid() >> 32 != pid)
		return 0;

	executed++;
	return 0;
}

SEC("uprobe.multi")
int test_uprobe_multi(struct pt_regs *ctx)
{
	if (bpf_get_current_pid_tgid() >> 32 != pid)
		return 0;

	executed++;
	return 0;
}

SEC("uretprobe.multi")
int test_uretprobe_multi(struct pt_regs *ctx)
{
	if (bpf_get_current_pid_tgid() >> 32 != pid)
		return 0;

	executed++;
	return 0;
}

SEC("uprobe.session")
int test_uprobe_session(struct pt_regs *ctx)
{
	if (bpf_get_current_pid_tgid() >> 32 != pid)
		return 0;

	executed++;
	return 0;
}

SEC("usdt")
int test_usdt(struct pt_regs *ctx)
{
	if (bpf_get_current_pid_tgid() >> 32 != pid)
		return 0;

	executed++;
	return 0;
}
