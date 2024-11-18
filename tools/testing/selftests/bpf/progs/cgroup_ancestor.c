// SPDX-License-Identifier: GPL-2.0
// Copyright (c) 2018 Facebook

#include <vmlinux.h>
#include <bpf/bpf_helpers.h>
#include <bpf/bpf_core_read.h>
#include "bpf_tracing_net.h"
#define NUM_CGROUP_LEVELS	4

__u64 cgroup_ids[NUM_CGROUP_LEVELS];
__u16 dport;

static __always_inline void log_nth_level(struct __sk_buff *skb, __u32 level)
{
	/* [1] &level passed to external function that may change it, it's
	 *     incompatible with loop unroll.
	 */
	cgroup_ids[level] = bpf_skb_ancestor_cgroup_id(skb, level);
}

SEC("tc")
int log_cgroup_id(struct __sk_buff *skb)
{
	struct sock *sk = (void *)skb->sk;

	if (!sk)
		return TC_ACT_OK;

	sk = bpf_core_cast(sk, struct sock);
	if (sk->sk_protocol == IPPROTO_UDP && sk->sk_dport == dport) {
		log_nth_level(skb, 0);
		log_nth_level(skb, 1);
		log_nth_level(skb, 2);
		log_nth_level(skb, 3);
	}

	return TC_ACT_OK;
}

char _license[] SEC("license") = "GPL";
