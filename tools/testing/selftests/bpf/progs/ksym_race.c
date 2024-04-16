// SPDX-License-Identifier: GPL-2.0
#include <vmlinux.h>
#include <bpf/bpf_helpers.h>

extern int bpf_testmod_ksym_percpu __ksym;

SEC("tc")
int ksym_fail(struct __sk_buff *ctx)
{
	return *(int *)bpf_this_cpu_ptr(&bpf_testmod_ksym_percpu);
}

char _license[] SEC("license") = "GPL";
