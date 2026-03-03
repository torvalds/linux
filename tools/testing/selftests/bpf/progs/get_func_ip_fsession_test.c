// SPDX-License-Identifier: GPL-2.0
#include "vmlinux.h"
#include <bpf/bpf_helpers.h>
#include <bpf/bpf_tracing.h>

char _license[] SEC("license") = "GPL";

__u64 test1_entry_result = 0;
__u64 test1_exit_result = 0;

SEC("fsession/bpf_fentry_test1")
int BPF_PROG(test1, int a)
{
	__u64 addr = bpf_get_func_ip(ctx);

	if (bpf_session_is_return(ctx))
		test1_exit_result = (const void *) addr == &bpf_fentry_test1;
	else
		test1_entry_result = (const void *) addr == &bpf_fentry_test1;
	return 0;
}
