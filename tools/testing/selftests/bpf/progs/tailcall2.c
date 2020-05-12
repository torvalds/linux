// SPDX-License-Identifier: GPL-2.0
#include <linux/bpf.h>

#include <bpf/bpf_helpers.h>

struct {
	__uint(type, BPF_MAP_TYPE_PROG_ARRAY);
	__uint(max_entries, 5);
	__uint(key_size, sizeof(__u32));
	__uint(value_size, sizeof(__u32));
} jmp_table SEC(".maps");

SEC("classifier/0")
int bpf_func_0(struct __sk_buff *skb)
{
	bpf_tail_call(skb, &jmp_table, 1);
	return 0;
}

SEC("classifier/1")
int bpf_func_1(struct __sk_buff *skb)
{
	bpf_tail_call(skb, &jmp_table, 2);
	return 1;
}

SEC("classifier/2")
int bpf_func_2(struct __sk_buff *skb)
{
	return 2;
}

SEC("classifier/3")
int bpf_func_3(struct __sk_buff *skb)
{
	bpf_tail_call(skb, &jmp_table, 4);
	return 3;
}

SEC("classifier/4")
int bpf_func_4(struct __sk_buff *skb)
{
	bpf_tail_call(skb, &jmp_table, 3);
	return 4;
}

SEC("classifier")
int entry(struct __sk_buff *skb)
{
	bpf_tail_call(skb, &jmp_table, 0);
	/* Check multi-prog update. */
	bpf_tail_call(skb, &jmp_table, 2);
	/* Check tail call limit. */
	bpf_tail_call(skb, &jmp_table, 3);
	return 3;
}

char __license[] SEC("license") = "GPL";
int _version SEC("version") = 1;
