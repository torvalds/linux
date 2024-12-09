// SPDX-License-Identifier: GPL-2.0
#include <linux/bpf.h>
#include <bpf/bpf_helpers.h>
#include <bpf/bpf_tracing.h>
#include <stdbool.h>
#include "bpf_kfuncs.h"
#include "bpf_misc.h"

char _license[] SEC("license") = "GPL";

__u64 uprobe_result[4];

SEC("uprobe.multi")
int uprobe_0(struct pt_regs *ctx)
{
	uprobe_result[0]++;
	return 0;
}

SEC("uprobe.multi")
int uprobe_1(struct pt_regs *ctx)
{
	uprobe_result[1]++;
	return 0;
}

SEC("uprobe.multi")
int uprobe_2(struct pt_regs *ctx)
{
	uprobe_result[2]++;
	return 0;
}

SEC("uprobe.multi")
int uprobe_3(struct pt_regs *ctx)
{
	uprobe_result[3]++;
	return 0;
}
