// SPDX-License-Identifier: GPL-2.0
#include <linux/bpf.h>

#include <bpf/bpf_helpers.h>

struct {
	__uint(type, BPF_MAP_TYPE_PROG_ARRAY);
	__uint(max_entries, 1);
	__uint(key_size, sizeof(__u32));
	__uint(value_size, sizeof(__u32));
} jmp_table SEC(".maps");

int count, which;

SEC("tc")
int classifier_0(struct __sk_buff *skb)
{
	count++;
	if (__builtin_constant_p(which))
		__bpf_unreachable();
	bpf_tail_call(skb, &jmp_table, which);
	return 1;
}

SEC("tc")
int entry(struct __sk_buff *skb)
{
	if (__builtin_constant_p(which))
		__bpf_unreachable();
	bpf_tail_call(skb, &jmp_table, which);
	return 0;
}

char __license[] SEC("license") = "GPL";
