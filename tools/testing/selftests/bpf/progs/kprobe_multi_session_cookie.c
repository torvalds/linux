// SPDX-License-Identifier: GPL-2.0
#include <linux/bpf.h>
#include <bpf/bpf_helpers.h>
#include <bpf/bpf_tracing.h>
#include <stdbool.h>
#include "bpf_kfuncs.h"

char _license[] SEC("license") = "GPL";

int pid = 0;

__u64 test_kprobe_1_result = 0;
__u64 test_kprobe_2_result = 0;
__u64 test_kprobe_3_result = 0;

/*
 * No tests in here, just to trigger 'bpf_fentry_test*'
 * through tracing test_run
 */
SEC("fentry/bpf_modify_return_test")
int BPF_PROG(trigger)
{
	return 0;
}

static int check_cookie(__u64 val, __u64 *result)
{
	long *cookie;

	if (bpf_get_current_pid_tgid() >> 32 != pid)
		return 1;

	cookie = bpf_session_cookie();

	if (bpf_session_is_return())
		*result = *cookie == val ? val : 0;
	else
		*cookie = val;
	return 0;
}

SEC("kprobe.session/bpf_fentry_test1")
int test_kprobe_1(struct pt_regs *ctx)
{
	return check_cookie(1, &test_kprobe_1_result);
}

SEC("kprobe.session/bpf_fentry_test1")
int test_kprobe_2(struct pt_regs *ctx)
{
	return check_cookie(2, &test_kprobe_2_result);
}

SEC("kprobe.session/bpf_fentry_test1")
int test_kprobe_3(struct pt_regs *ctx)
{
	return check_cookie(3, &test_kprobe_3_result);
}
