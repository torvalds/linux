// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2021 Facebook */
#include <vmlinux.h>
#include <bpf/bpf_helpers.h>

extern long bpf_kfunc_call_test4(signed char a, short b, int c, long d) __ksym;
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
extern int *bpf_kfunc_call_test_get_rdwr_mem(struct prog_test_ref_kfunc *p, const int rdwr_buf_size) __ksym;
extern int *bpf_kfunc_call_test_get_rdonly_mem(struct prog_test_ref_kfunc *p, const int rdonly_buf_size) __ksym;
extern u32 bpf_kfunc_call_test_static_unused_arg(u32 arg, u32 unused) __ksym;

SEC("tc")
int kfunc_call_test4(struct __sk_buff *skb)
{
	struct bpf_sock *sk = skb->sk;
	long tmp;

	if (!sk)
		return -1;

	sk = bpf_sk_fullsock(sk);
	if (!sk)
		return -1;

	tmp = bpf_kfunc_call_test4(-3, -30, -200, -1000);
	return (tmp >> 32) + tmp;
}

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

struct syscall_test_args {
	__u8 data[16];
	size_t size;
};

SEC("syscall")
int kfunc_syscall_test(struct syscall_test_args *args)
{
	const long size = args->size;

	if (size > sizeof(args->data))
		return -7; /* -E2BIG */

	bpf_kfunc_call_test_mem_len_pass1(&args->data, sizeof(args->data));
	bpf_kfunc_call_test_mem_len_pass1(&args->data, sizeof(*args));
	bpf_kfunc_call_test_mem_len_pass1(&args->data, size);

	return 0;
}

SEC("syscall")
int kfunc_syscall_test_null(struct syscall_test_args *args)
{
	/* Must be called with args as a NULL pointer
	 * we do not check for it to have the verifier consider that
	 * the pointer might not be null, and so we can load it.
	 *
	 * So the following can not be added:
	 *
	 * if (args)
	 *      return -22;
	 */

	bpf_kfunc_call_test_mem_len_pass1(args, 0);

	return 0;
}

SEC("tc")
int kfunc_call_test_get_mem(struct __sk_buff *skb)
{
	struct prog_test_ref_kfunc *pt;
	unsigned long s = 0;
	int *p = NULL;
	int ret = 0;

	pt = bpf_kfunc_call_test_acquire(&s);
	if (pt) {
		p = bpf_kfunc_call_test_get_rdwr_mem(pt, 2 * sizeof(int));
		if (p) {
			p[0] = 42;
			ret = p[1]; /* 108 */
		} else {
			ret = -1;
		}

		if (ret >= 0) {
			p = bpf_kfunc_call_test_get_rdonly_mem(pt, 2 * sizeof(int));
			if (p)
				ret = p[0]; /* 42 */
			else
				ret = -1;
		}

		bpf_kfunc_call_test_release(pt);
	}
	return ret;
}

SEC("tc")
int kfunc_call_test_static_unused_arg(struct __sk_buff *skb)
{

	u32 expected = 5, actual;

	actual = bpf_kfunc_call_test_static_unused_arg(expected, 0xdeadbeef);
	return actual != expected ? -1 : 0;
}

char _license[] SEC("license") = "GPL";
