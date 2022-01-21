// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2021 Facebook */
#include <linux/bpf.h>
#include <bpf/bpf_helpers.h>
#include "bpf_tcp_helpers.h"

extern int bpf_kfunc_call_test2(struct sock *sk, __u32 a, __u32 b) __ksym;
extern __u64 bpf_kfunc_call_test1(struct sock *sk, __u32 a, __u64 b,
				  __u32 c, __u64 d) __ksym;

SEC("tc")
int kfunc_call_test2(struct __sk_buff *skb)
{
	struct bpf_sock *sk = skb->sk;

	if (!sk)
		return -1;

	sk = bpf_sk_fullsock(sk);
	if (!sk)
		return -1;

	return bpf_kfunc_call_test2((struct sock *)sk, 1, 2);
}

SEC("tc")
int kfunc_call_test1(struct __sk_buff *skb)
{
	struct bpf_sock *sk = skb->sk;
	__u64 a = 1ULL << 32;
	__u32 ret;

	if (!sk)
		return -1;

	sk = bpf_sk_fullsock(sk);
	if (!sk)
		return -1;

	a = bpf_kfunc_call_test1((struct sock *)sk, 1, a | 2, 3, a | 4);
	ret = a >> 32;   /* ret should be 2 */
	ret += (__u32)a; /* ret should be 12 */

	return ret;
}

char _license[] SEC("license") = "GPL";
