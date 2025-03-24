// SPDX-License-Identifier: GPL-2.0
#include "vmlinux.h"
#include <bpf/bpf_helpers.h>
#include <bpf/bpf_tracing.h>

char _license[] SEC("license") = "GPL";

__u32 pids[3];
__u32 test[3][2];

static void update_pid(int idx)
{
	__u32 pid = bpf_get_current_pid_tgid() >> 32;

	if (pid == pids[idx])
		test[idx][0]++;
	else
		test[idx][1]++;
}

SEC("uprobe.multi")
int uprobe_multi_0(struct pt_regs *ctx)
{
	update_pid(0);
	return 0;
}

SEC("uprobe.multi")
int uprobe_multi_1(struct pt_regs *ctx)
{
	update_pid(1);
	return 0;
}

SEC("uprobe.multi")
int uprobe_multi_2(struct pt_regs *ctx)
{
	update_pid(2);
	return 0;
}
