// SPDX-License-Identifier: GPL-2.0

#include <linux/bpf.h>
#include <bpf/bpf_helpers.h>
#include "bpf_misc.h"

__noinline
int subprog_tc(struct __sk_buff *skb)
{
	int ret = 1;

	__sink(skb);
	__sink(ret);
	/* let verifier know that 'subprog_tc' can change pointers to skb->data */
	bpf_skb_change_proto(skb, 0, 0);
	return ret;
}

SEC("tc")
int entry_tc(struct __sk_buff *skb)
{
	return subprog_tc(skb);
}

char __license[] SEC("license") = "GPL";
