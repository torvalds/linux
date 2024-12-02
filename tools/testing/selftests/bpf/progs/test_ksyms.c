// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2019 Facebook */

#include <stdbool.h>
#include <linux/bpf.h>
#include <bpf/bpf_helpers.h>

__u64 out__bpf_link_fops = -1;
__u64 out__bpf_link_fops1 = -1;
__u64 out__btf_size = -1;
__u64 out__per_cpu_start = -1;

extern const void bpf_link_fops __ksym;
extern const void __start_BTF __ksym;
extern const void __stop_BTF __ksym;
extern const void __per_cpu_start __ksym;
/* non-existing symbol, weak, default to zero */
extern const void bpf_link_fops1 __ksym __weak;

SEC("raw_tp/sys_enter")
int handler(const void *ctx)
{
	out__bpf_link_fops = (__u64)&bpf_link_fops;
	out__btf_size = (__u64)(&__stop_BTF - &__start_BTF);
	out__per_cpu_start = (__u64)&__per_cpu_start;

	out__bpf_link_fops1 = (__u64)&bpf_link_fops1;

	return 0;
}

char _license[] SEC("license") = "GPL";
