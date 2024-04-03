// SPDX-License-Identifier: GPL-2.0
#include <linux/bpf.h>
#include <bpf/bpf_helpers.h>
#include <bpf/bpf_tracing.h>

char _license[] SEC("license") = "GPL";

struct {
	__uint(type, BPF_MAP_TYPE_PROG_ARRAY);
	__uint(max_entries, 1);
	__uint(key_size, sizeof(__u32));
	__uint(value_size, sizeof(__u32));
} jmp_table SEC(".maps");

SEC("?fentry/bpf_fentry_test1")
int BPF_PROG(test, int a)
{
	bpf_tail_call_static(ctx, &jmp_table, 0);
	return 0;
}

SEC("fentry/bpf_fentry_test1")
int BPF_PROG(call1, int a)
{
	return 0;
}

SEC("fentry/bpf_fentry_test1")
int BPF_PROG(call2, int a)
{
	return 0;
}
