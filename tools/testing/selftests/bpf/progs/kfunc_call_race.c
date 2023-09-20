// SPDX-License-Identifier: GPL-2.0
#include <vmlinux.h>
#include <bpf/bpf_helpers.h>
#include "../bpf_testmod/bpf_testmod_kfunc.h"

SEC("tc")
int kfunc_call_fail(struct __sk_buff *ctx)
{
	bpf_testmod_test_mod_kfunc(0);
	return 0;
}

char _license[] SEC("license") = "GPL";
