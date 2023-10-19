// SPDX-License-Identifier: GPL-2.0
#include <linux/bpf.h>
#include <bpf/bpf_helpers.h>

#define __unused __attribute__((unused))

struct {
	__uint(type, BPF_MAP_TYPE_PROG_ARRAY);
	__uint(max_entries, 1);
	__uint(key_size, sizeof(__u32));
	__uint(value_size, sizeof(__u32));
} jmp_table SEC(".maps");

int done = 0;

SEC("tc")
int classifier_0(struct __sk_buff *skb __unused)
{
	done = 1;
	return 0;
}

static __noinline
int subprog_tail(struct __sk_buff *skb)
{
	/* Don't propagate the constant to the caller */
	volatile int ret = 1;

	bpf_tail_call_static(skb, &jmp_table, 0);
	return ret;
}

SEC("tc")
int entry(struct __sk_buff *skb)
{
	/* Have data on stack which size is not a multiple of 8 */
	volatile char arr[1] = {};

	return subprog_tail(skb);
}

char __license[] SEC("license") = "GPL";
