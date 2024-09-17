// SPDX-License-Identifier: GPL-2.0
#include <linux/bpf.h>
#include <bpf/bpf_helpers.h>

struct {
	__uint(type, BPF_MAP_TYPE_PROG_ARRAY);
	__uint(max_entries, 2);
	__uint(key_size, sizeof(__u32));
	__uint(value_size, sizeof(__u32));
} jmp_table SEC(".maps");

#define TAIL_FUNC(x) 				\
	SEC("tc")				\
	int classifier_##x(struct __sk_buff *skb)	\
	{					\
		return x;			\
	}
TAIL_FUNC(0)
TAIL_FUNC(1)

static __noinline
int subprog_tail(struct __sk_buff *skb)
{
	bpf_tail_call_static(skb, &jmp_table, 0);

	return skb->len * 2;
}

SEC("tc")
int entry(struct __sk_buff *skb)
{
	bpf_tail_call_static(skb, &jmp_table, 1);

	return subprog_tail(skb);
}

char __license[] SEC("license") = "GPL";
