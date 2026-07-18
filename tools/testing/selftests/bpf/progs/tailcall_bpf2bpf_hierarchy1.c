// SPDX-License-Identifier: GPL-2.0
#include <linux/bpf.h>
#include <bpf/bpf_helpers.h>
#include "bpf_legacy.h"
#include "bpf_test_utils.h"

struct {
	__uint(type, BPF_MAP_TYPE_PROG_ARRAY);
	__uint(max_entries, 1);
	__uint(key_size, sizeof(__u32));
	__uint(value_size, sizeof(__u32));
} jmp_table SEC(".maps");

int count = 0;

static __noinline
int subprog_tail(struct __sk_buff *skb)
{
	int ret = 0;

	bpf_tail_call_static(skb, &jmp_table, 0);
	barrier_var(ret);
	return ret;
}

SEC("tc")
int entry(struct __sk_buff *skb)
{
	int ret = 1, ret1, ret2;

	clobber_regs_stack();

	count++;
	ret1 = subprog_tail(skb);
	ret2 = subprog_tail(skb);
	__sink(ret1);
	__sink(ret2);

	return ret;
}

char __license[] SEC("license") = "GPL";
