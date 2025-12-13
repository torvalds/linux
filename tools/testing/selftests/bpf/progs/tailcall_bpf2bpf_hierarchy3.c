// SPDX-License-Identifier: GPL-2.0
#include <linux/bpf.h>
#include <bpf/bpf_helpers.h>
#include "bpf_misc.h"
#include "bpf_test_utils.h"

int classifier_0(struct __sk_buff *skb);

struct {
	__uint(type, BPF_MAP_TYPE_PROG_ARRAY);
	__uint(max_entries, 1);
	__uint(key_size, sizeof(__u32));
	__array(values, void (void));
} jmp_table0 SEC(".maps") = {
	.values = {
		[0] = (void *) &classifier_0,
	},
};

struct {
	__uint(type, BPF_MAP_TYPE_PROG_ARRAY);
	__uint(max_entries, 1);
	__uint(key_size, sizeof(__u32));
	__array(values, void (void));
} jmp_table1 SEC(".maps") = {
	.values = {
		[0] = (void *) &classifier_0,
	},
};

int count = 0;

static __noinline
int subprog_tail(struct __sk_buff *skb, void *jmp_table)
{
	bpf_tail_call_static(skb, jmp_table, 0);
	return 0;
}

__auxiliary
SEC("tc")
int classifier_0(struct __sk_buff *skb)
{
	count++;
	subprog_tail(skb, &jmp_table0);
	subprog_tail(skb, &jmp_table1);
	return count;
}

__success
__retval(33)
SEC("tc")
int tailcall_bpf2bpf_hierarchy_3(struct __sk_buff *skb)
{
	int ret = 0;

	clobber_regs_stack();

	bpf_tail_call_static(skb, &jmp_table0, 0);

	__sink(ret);
	return ret;
}

char __license[] SEC("license") = "GPL";
