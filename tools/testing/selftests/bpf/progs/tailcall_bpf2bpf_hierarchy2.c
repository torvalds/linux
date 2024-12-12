// SPDX-License-Identifier: GPL-2.0
#include <linux/bpf.h>
#include <bpf/bpf_helpers.h>
#include "bpf_misc.h"

int classifier_0(struct __sk_buff *skb);
int classifier_1(struct __sk_buff *skb);

struct {
	__uint(type, BPF_MAP_TYPE_PROG_ARRAY);
	__uint(max_entries, 2);
	__uint(key_size, sizeof(__u32));
	__array(values, void (void));
} jmp_table SEC(".maps") = {
	.values = {
		[0] = (void *) &classifier_0,
		[1] = (void *) &classifier_1,
	},
};

int count0 = 0;
int count1 = 0;

static __noinline
int subprog_tail0(struct __sk_buff *skb)
{
	bpf_tail_call_static(skb, &jmp_table, 0);
	return 0;
}

__auxiliary
SEC("tc")
int classifier_0(struct __sk_buff *skb)
{
	count0++;
	subprog_tail0(skb);
	return 0;
}

static __noinline
int subprog_tail1(struct __sk_buff *skb)
{
	bpf_tail_call_static(skb, &jmp_table, 1);
	return 0;
}

__auxiliary
SEC("tc")
int classifier_1(struct __sk_buff *skb)
{
	count1++;
	subprog_tail1(skb);
	return 0;
}

__success
__retval(33)
SEC("tc")
int tailcall_bpf2bpf_hierarchy_2(struct __sk_buff *skb)
{
	int ret = 0;

	subprog_tail0(skb);
	subprog_tail1(skb);

	__sink(ret);
	return (count1 << 16) | count0;
}

char __license[] SEC("license") = "GPL";
