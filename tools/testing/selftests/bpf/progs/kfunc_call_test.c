// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2021 Facebook */
#include <vmlinux.h>
#include <bpf/bpf_helpers.h>

extern int bpf_kfunc_call_test2(struct sock *sk, __u32 a, __u32 b) __ksym;
extern __u64 bpf_kfunc_call_test1(struct sock *sk, __u32 a, __u64 b,
				  __u32 c, __u64 d) __ksym;

extern struct prog_test_ref_kfunc *bpf_kfunc_call_test_acquire(unsigned long *sp) __ksym;
extern void bpf_kfunc_call_test_release(struct prog_test_ref_kfunc *p) __ksym;
extern void bpf_kfunc_call_test_pass_ctx(struct __sk_buff *skb) __ksym;
extern void bpf_kfunc_call_test_pass1(struct prog_test_pass1 *p) __ksym;
extern void bpf_kfunc_call_test_pass2(struct prog_test_pass2 *p) __ksym;
extern void bpf_kfunc_call_test_mem_len_pass1(void *mem, int len) __ksym;
extern void bpf_kfunc_call_test_mem_len_fail2(__u64 *mem, int len) __ksym;

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

SEC("tc")
int kfunc_call_test_ref_btf_id(struct __sk_buff *skb)
{
	struct prog_test_ref_kfunc *pt;
	unsigned long s = 0;
	int ret = 0;

	pt = bpf_kfunc_call_test_acquire(&s);
	if (pt) {
		if (pt->a != 42 || pt->b != 108)
			ret = -1;
		bpf_kfunc_call_test_release(pt);
	}
	return ret;
}

SEC("tc")
int kfunc_call_test_pass(struct __sk_buff *skb)
{
	struct prog_test_pass1 p1 = {};
	struct prog_test_pass2 p2 = {};
	short a = 0;
	__u64 b = 0;
	long c = 0;
	char d = 0;
	int e = 0;

	bpf_kfunc_call_test_pass_ctx(skb);
	bpf_kfunc_call_test_pass1(&p1);
	bpf_kfunc_call_test_pass2(&p2);

	bpf_kfunc_call_test_mem_len_pass1(&a, sizeof(a));
	bpf_kfunc_call_test_mem_len_pass1(&b, sizeof(b));
	bpf_kfunc_call_test_mem_len_pass1(&c, sizeof(c));
	bpf_kfunc_call_test_mem_len_pass1(&d, sizeof(d));
	bpf_kfunc_call_test_mem_len_pass1(&e, sizeof(e));
	bpf_kfunc_call_test_mem_len_fail2(&b, -1);

	return 0;
}

char _license[] SEC("license") = "GPL";
