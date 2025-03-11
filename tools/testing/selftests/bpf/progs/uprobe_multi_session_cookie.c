// SPDX-License-Identifier: GPL-2.0
#include <linux/bpf.h>
#include <bpf/bpf_helpers.h>
#include <bpf/bpf_tracing.h>
#include <stdbool.h>
#include "bpf_kfuncs.h"

char _license[] SEC("license") = "GPL";

int pid = 0;

__u64 test_uprobe_1_result = 0;
__u64 test_uprobe_2_result = 0;
__u64 test_uprobe_3_result = 0;

static int check_cookie(__u64 val, __u64 *result)
{
	__u64 *cookie;

	if (bpf_get_current_pid_tgid() >> 32 != pid)
		return 1;

	cookie = bpf_session_cookie();

	if (bpf_session_is_return())
		*result = *cookie == val ? val : 0;
	else
		*cookie = val;
	return 0;
}

SEC("uprobe.session//proc/self/exe:uprobe_multi_func_1")
int uprobe_1(struct pt_regs *ctx)
{
	return check_cookie(1, &test_uprobe_1_result);
}

SEC("uprobe.session//proc/self/exe:uprobe_multi_func_2")
int uprobe_2(struct pt_regs *ctx)
{
	return check_cookie(2, &test_uprobe_2_result);
}

SEC("uprobe.session//proc/self/exe:uprobe_multi_func_3")
int uprobe_3(struct pt_regs *ctx)
{
	return check_cookie(3, &test_uprobe_3_result);
}
