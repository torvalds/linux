// SPDX-License-Identifier: GPL-2.0
#include <linux/bpf.h>
#include <bpf/bpf_helpers.h>
#include <bpf/bpf_tracing.h>

SEC("fentry/bpf_modify_return_test")
int BPF_PROG(fentry_test, int a, int *b)
{
	return 0;
}

SEC("fmod_ret/bpf_modify_return_test")
int BPF_PROG(fmod_ret_test, int a, int *b, int ret)
{
	return 0;
}

SEC("fexit/bpf_modify_return_test")
int BPF_PROG(fexit_test, int a, int *b, int ret)
{
	return 0;
}

char _license[] SEC("license") = "GPL";
