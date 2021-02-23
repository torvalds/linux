// SPDX-License-Identifier: GPL-2.0
#include <linux/bpf.h>
#include <bpf/bpf_helpers.h>

struct {
	__uint(type, BPF_MAP_TYPE_PROG_ARRAY);
	__uint(max_entries, 3);
	__uint(key_size, sizeof(__u32));
	__uint(value_size, sizeof(__u32));
} jmp_table SEC(".maps");

static volatile int count;

__noinline
int subprog_tail_2(struct __sk_buff *skb)
{
	bpf_tail_call_static(skb, &jmp_table, 2);
	return skb->len * 3;
}

__noinline
int subprog_tail_1(struct __sk_buff *skb)
{
	bpf_tail_call_static(skb, &jmp_table, 1);
	return skb->len * 2;
}

__noinline
int subprog_tail(struct __sk_buff *skb)
{
	bpf_tail_call_static(skb, &jmp_table, 0);
	return skb->len;
}

SEC("classifier/1")
int bpf_func_1(struct __sk_buff *skb)
{
	return subprog_tail_2(skb);
}

SEC("classifier/2")
int bpf_func_2(struct __sk_buff *skb)
{
	count++;
	return subprog_tail_2(skb);
}

SEC("classifier/0")
int bpf_func_0(struct __sk_buff *skb)
{
	return subprog_tail_1(skb);
}

SEC("classifier")
int entry(struct __sk_buff *skb)
{
	return subprog_tail(skb);
}

char __license[] SEC("license") = "GPL";
int _version SEC("version") = 1;
