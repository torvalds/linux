// SPDX-License-Identifier: GPL-2.0
#include <vmlinux.h>
#include <bpf/bpf_helpers.h>
#include "../test_kmods/bpf_testmod_kfunc.h"

SEC("tc")
int kfunc_destructive_test(void)
{
	bpf_kfunc_call_test_destructive();
	return 0;
}

char _license[] SEC("license") = "GPL";
