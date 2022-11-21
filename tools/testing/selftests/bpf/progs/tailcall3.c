// SPDX-License-Identifier: GPL-2.0
#include <linux/bpf.h>

#include <bpf/bpf_helpers.h>

struct {
	__uint(type, BPF_MAP_TYPE_PROG_ARRAY);
	__uint(max_entries, 1);
	__uint(key_size, sizeof(__u32));
	__uint(value_size, sizeof(__u32));
} jmp_table SEC(".maps");

int count = 0;

SEC("tc")
int classifier_0(struct __sk_buff *skb)
{
	count++;
	bpf_tail_call_static(skb, &jmp_table, 0);
	return 1;
}

SEC("tc")
int entry(struct __sk_buff *skb)
{
	bpf_tail_call_static(skb, &jmp_table, 0);
	return 0;
}

char __license[] SEC("license") = "GPL";
