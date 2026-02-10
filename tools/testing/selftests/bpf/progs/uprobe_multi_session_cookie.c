// SPDX-License-Identifier: GPL-2.0
#include <vmlinux.h>
#include <bpf/bpf_helpers.h>
#include <bpf/bpf_tracing.h>
#include <stdbool.h>

char _license[] SEC("license") = "GPL";

int pid = 0;

__u64 test_uprobe_1_result = 0;
__u64 test_uprobe_2_result = 0;
__u64 test_uprobe_3_result = 0;

static int check_cookie(struct pt_regs *ctx, __u64 val, __u64 *result)
{
	__u64 *cookie;

	if (bpf_get_current_pid_tgid() >> 32 != pid)
		return 1;

	cookie = bpf_session_cookie(ctx);

	if (bpf_session_is_return(ctx))
		*result = *cookie == val ? val : 0;
	else
		*cookie = val;
	return 0;
}

SEC("uprobe.session//proc/self/exe:uprobe_multi_func_1")
int uprobe_1(struct pt_regs *ctx)
{
	return check_cookie(ctx, 1, &test_uprobe_1_result);
}

SEC("uprobe.session//proc/self/exe:uprobe_multi_func_2")
int uprobe_2(struct pt_regs *ctx)
{
	return check_cookie(ctx, 2, &test_uprobe_2_result);
}

SEC("uprobe.session//proc/self/exe:uprobe_multi_func_3")
int uprobe_3(struct pt_regs *ctx)
{
	return check_cookie(ctx, 3, &test_uprobe_3_result);
}
