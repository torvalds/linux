// SPDX-License-Identifier: GPL-2.0
#include <vmlinux.h>
#include <bpf/bpf_helpers.h>
#include <bpf/bpf_tracing.h>

char _license[] SEC("license") = "GPL";

__hidden extern int tracing_multi_arg_check(__u64 *ctx, __u64 *test_result, bool is_return);

__u64 test_result_fentry = 0;
__u64 test_result_fexit = 0;

SEC("fentry.multi/bpf_testmod:bpf_testmod_fentry_test*")
int BPF_PROG(test_fentry)
{
	tracing_multi_arg_check(ctx, &test_result_fentry, false);
	return 0;
}

SEC("fexit.multi/bpf_testmod:bpf_testmod_fentry_test*")
int BPF_PROG(test_fexit)
{
	tracing_multi_arg_check(ctx, &test_result_fexit, true);
	return 0;
}
