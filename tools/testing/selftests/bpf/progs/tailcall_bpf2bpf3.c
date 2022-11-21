// SPDX-License-Identifier: GPL-2.0
#include <linux/bpf.h>
#include <bpf/bpf_helpers.h>
#include "bpf_legacy.h"

struct {
	__uint(type, BPF_MAP_TYPE_PROG_ARRAY);
	__uint(max_entries, 2);
	__uint(key_size, sizeof(__u32));
	__uint(value_size, sizeof(__u32));
} jmp_table SEC(".maps");

__noinline
int subprog_tail2(struct __sk_buff *skb)
{
	volatile char arr[64] = {};

	if (load_word(skb, 0) || load_half(skb, 0))
		bpf_tail_call_static(skb, &jmp_table, 10);
	else
		bpf_tail_call_static(skb, &jmp_table, 1);

	return skb->len;
}

static __noinline
int subprog_tail(struct __sk_buff *skb)
{
	volatile char arr[64] = {};

	bpf_tail_call_static(skb, &jmp_table, 0);

	return skb->len * 2;
}

SEC("tc")
int classifier_0(struct __sk_buff *skb)
{
	volatile char arr[128] = {};

	return subprog_tail2(skb);
}

SEC("tc")
int classifier_1(struct __sk_buff *skb)
{
	volatile char arr[128] = {};

	return skb->len * 3;
}

SEC("tc")
int entry(struct __sk_buff *skb)
{
	volatile char arr[128] = {};

	return subprog_tail(skb);
}

char __license[] SEC("license") = "GPL";
