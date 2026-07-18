// SPDX-License-Identifier: GPL-2.0
#include <vmlinux.h>
#include <bpf/bpf_helpers.h>
#include <bpf/bpf_tracing.h>

char _license[] SEC("license") = "GPL";

SEC("fentry.multi")
int BPF_PROG(test_fentry)
{
	return 0;
}

SEC("fentry.multi.s")
int BPF_PROG(test_fentry_s)
{
	return 0;
}
