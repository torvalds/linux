// SPDX-License-Identifier: GPL-2.0
#include <vmlinux.h>
#include <bpf/bpf_helpers.h>
#include <bpf/bpf_tracing.h>

char _license[] SEC("license") = "GPL";

__hidden extern int tracing_multi_arg_check(__u64 *ctx, __u64 *test_result, bool is_return);

__u64 test_result_fentry = 0;
__u64 test_result_fexit = 0;

SEC("fsession.multi/bpf_fentry_test*")
int BPF_PROG(test_session_1)
{
	volatile __u64 *cookie = bpf_session_cookie(ctx);

	if (bpf_session_is_return(ctx)) {
		if (tracing_multi_arg_check(ctx, &test_result_fexit, true))
			return 0;
		/* extra count for test_result_fexit cookie */
		test_result_fexit += *cookie == 0xbeafbeafbeafbeaf;
	} else {
		if (tracing_multi_arg_check(ctx, &test_result_fentry, false))
			return 0;
		*cookie = 0xbeafbeafbeafbeaf;
	}
	return 0;
}

SEC("fsession.multi.s/bpf_fentry_test1")
int BPF_PROG(test_fsession_s)
{
	volatile __u64 *cookie = bpf_session_cookie(ctx);

	if (bpf_session_is_return(ctx)) {
		if (tracing_multi_arg_check(ctx, &test_result_fexit, true))
			return 0;
		/* extra count for test_result_fexit cookie */
		test_result_fexit += *cookie == 0xbeafbeafbeafbeaf;
	} else {
		if (tracing_multi_arg_check(ctx, &test_result_fentry, false))
			return 0;
		*cookie = 0xbeafbeafbeafbeaf;
	}
	return 0;
}

SEC("fsession.multi/bpf_testmod:bpf_testmod_fentry_test*")
int BPF_PROG(test_session_2)
{
	volatile __u64 *cookie = bpf_session_cookie(ctx);

	if (bpf_session_is_return(ctx)) {
		if (tracing_multi_arg_check(ctx, &test_result_fexit, true))
			return 0;
		/* extra count for test_result_fexit cookie */
		test_result_fexit += *cookie == 0xbeafbeafbeafbeaf;
	} else {
		if (tracing_multi_arg_check(ctx, &test_result_fentry, false))
			return 0;
		*cookie = 0xbeafbeafbeafbeaf;
	}
	return 0;
}
