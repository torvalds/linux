// SPDX-License-Identifier: GPL-2.0
#include "vmlinux.h"
#include <bpf/bpf_helpers.h>
#include <bpf/bpf_tracing.h>
#include "bpf_misc.h"

char _license[] SEC("license") = "GPL";

SEC("fentry.multi/bpf_fentry_test1")
__failure
__msg("func 'bpf_multi_func' doesn't have 1-th argument")
int BPF_PROG(fentry_direct_access, int a)
{
	return a;
}

SEC("fexit.multi/bpf_fentry_test3")
__failure
__msg("invalid bpf_context access off=24 size=8")
int BPF_PROG(fexit_direct_access, char a, int b, __u64 c, int ret)
{
	return ret;
}

SEC("fsession.multi/bpf_fentry_test4")
__failure
__msg("invalid bpf_context access off=16 size=8")
int BPF_PROG(fsession_direct_access, void *a, char b, int c, __u64 d, int ret)
{
	return c;
}
