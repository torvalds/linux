// SPDX-License-Identifier: GPL-2.0
#include <linux/bpf.h>

#include <bpf/bpf_helpers.h>

struct {
	__uint(type, BPF_MAP_TYPE_PROG_ARRAY);
	__uint(max_entries, 5);
	__uint(key_size, sizeof(__u32));
	__uint(value_size, sizeof(__u32));
} jmp_table SEC(".maps");

SEC("tc")
int classifier_0(struct __sk_buff *skb)
{
	bpf_tail_call_static(skb, &jmp_table, 1);
	return 0;
}

SEC("tc")
int classifier_1(struct __sk_buff *skb)
{
	bpf_tail_call_static(skb, &jmp_table, 2);
	return 1;
}

SEC("tc")
int classifier_2(struct __sk_buff *skb)
{
	return 2;
}

SEC("tc")
int classifier_3(struct __sk_buff *skb)
{
	bpf_tail_call_static(skb, &jmp_table, 4);
	return 3;
}

SEC("tc")
int classifier_4(struct __sk_buff *skb)
{
	bpf_tail_call_static(skb, &jmp_table, 3);
	return 4;
}

SEC("tc")
int entry(struct __sk_buff *skb)
{
	bpf_tail_call_static(skb, &jmp_table, 0);
	/* Check multi-prog update. */
	bpf_tail_call_static(skb, &jmp_table, 2);
	/* Check tail call limit. */
	bpf_tail_call_static(skb, &jmp_table, 3);
	return 3;
}

char __license[] SEC("license") = "GPL";
