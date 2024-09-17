// SPDX-License-Identifier: GPL-2.0
#include <linux/bpf.h>

#include <bpf/bpf_helpers.h>

struct {
	__uint(type, BPF_MAP_TYPE_PROG_ARRAY);
	__uint(max_entries, 3);
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
TAIL_FUNC(2)

SEC("tc")
int entry(struct __sk_buff *skb)
{
	/* Multiple locations to make sure we patch
	 * all of them.
	 */
	bpf_tail_call_static(skb, &jmp_table, 0);
	bpf_tail_call_static(skb, &jmp_table, 0);
	bpf_tail_call_static(skb, &jmp_table, 0);
	bpf_tail_call_static(skb, &jmp_table, 0);

	bpf_tail_call_static(skb, &jmp_table, 1);
	bpf_tail_call_static(skb, &jmp_table, 1);
	bpf_tail_call_static(skb, &jmp_table, 1);
	bpf_tail_call_static(skb, &jmp_table, 1);

	bpf_tail_call_static(skb, &jmp_table, 2);
	bpf_tail_call_static(skb, &jmp_table, 2);
	bpf_tail_call_static(skb, &jmp_table, 2);
	bpf_tail_call_static(skb, &jmp_table, 2);

	return 3;
}

char __license[] SEC("license") = "GPL";
