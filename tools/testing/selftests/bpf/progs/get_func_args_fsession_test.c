// SPDX-License-Identifier: GPL-2.0
#include "vmlinux.h"
#include <bpf/bpf_helpers.h>
#include <bpf/bpf_tracing.h>
#include <errno.h>

char _license[] SEC("license") = "GPL";

__u64 test1_result = 0;

SEC("fsession/bpf_fentry_test1")
int BPF_PROG(test1)
{
	__u64 cnt = bpf_get_func_arg_cnt(ctx);
	__u64 a = 0, z = 0, ret = 0;
	__s64 err;

	test1_result = cnt == 1;

	/* valid arguments */
	err = bpf_get_func_arg(ctx, 0, &a);
	test1_result &= err == 0 && ((int) a == 1);

	/* not valid argument */
	err = bpf_get_func_arg(ctx, 1, &z);
	test1_result &= err == -EINVAL;

	if (bpf_session_is_return(ctx)) {
		err = bpf_get_func_ret(ctx, &ret);
		test1_result &= err == 0 && ret == 2;
	} else {
		err = bpf_get_func_ret(ctx, &ret);
		test1_result &= err == 0 && ret == 0;
	}

	return 0;
}
