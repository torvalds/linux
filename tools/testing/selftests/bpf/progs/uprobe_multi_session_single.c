// SPDX-License-Identifier: GPL-2.0
#include <linux/bpf.h>
#include <bpf/bpf_helpers.h>
#include <bpf/bpf_tracing.h>
#include <stdbool.h>
#include "bpf_kfuncs.h"
#include "bpf_misc.h"

char _license[] SEC("license") = "GPL";

__u64 uprobe_session_result[3] = {};
int pid = 0;

static int uprobe_multi_check(void *ctx, int idx)
{
	if (bpf_get_current_pid_tgid() >> 32 != pid)
		return 1;

	uprobe_session_result[idx]++;

	/* only consumer 1 executes return probe */
	if (idx == 0 || idx == 2)
		return 1;

	return 0;
}

SEC("uprobe.session//proc/self/exe:uprobe_multi_func_1")
int uprobe_0(struct pt_regs *ctx)
{
	return uprobe_multi_check(ctx, 0);
}

SEC("uprobe.session//proc/self/exe:uprobe_multi_func_1")
int uprobe_1(struct pt_regs *ctx)
{
	return uprobe_multi_check(ctx, 1);
}

SEC("uprobe.session//proc/self/exe:uprobe_multi_func_1")
int uprobe_2(struct pt_regs *ctx)
{
	return uprobe_multi_check(ctx, 2);
}
