// SPDX-License-Identifier: GPL-2.0
#include <vmlinux.h>
#include <bpf/bpf_helpers.h>
#include <bpf/bpf_tracing.h>

char _license[] SEC("license") = "GPL";

__hidden extern int tracing_multi_arg_check(__u64 *ctx, __u64 *test_result, bool is_return);

__u64 test_result_fentry_1 = 0;
__u64 test_result_fentry_2 = 0;
__u64 test_result_fexit_1 = 0;
__u64 test_result_fexit_2 = 0;

SEC("fentry.multi")
int BPF_PROG(fentry_1)
{
	tracing_multi_arg_check(ctx, &test_result_fentry_1, false);
	return 0;
}

SEC("fentry.multi")
int BPF_PROG(fentry_2)
{
	tracing_multi_arg_check(ctx, &test_result_fentry_2, false);
	return 0;
}

SEC("fexit.multi")
int BPF_PROG(fexit_1)
{
	tracing_multi_arg_check(ctx, &test_result_fexit_1, true);
	return 0;
}

SEC("fexit.multi")
int BPF_PROG(fexit_2)
{
	tracing_multi_arg_check(ctx, &test_result_fexit_2, true);
	return 0;
}
