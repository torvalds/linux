// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2021 Facebook */
#include <vmlinux.h>
#include <bpf/bpf_helpers.h>
#include "bpf_misc.h"
#include "../test_kmods/bpf_testmod_kfunc.h"

SEC("tc")
int kfunc_call_test5(struct __sk_buff *skb)
{
	struct bpf_sock *sk = skb->sk;
	int ret;
	u32 val32;
	u16 val16;
	u8 val8;

	if (!sk)
		return -1;

	sk = bpf_sk_fullsock(sk);
	if (!sk)
		return -1;

	/*
	 * Test with constant values to verify zero-extension.
	 * ISA-dependent BPF asm:
	 *   With ALU32:    w1 = 0xFF; w2 = 0xFFFF; w3 = 0xFFFFffff
	 *   Without ALU32: r1 = 0xFF; r2 = 0xFFFF; r3 = 0xFFFFffff
	 * Both zero-extend to 64-bit before the kfunc call.
	 */
	ret = bpf_kfunc_call_test5(0xFF, 0xFFFF, 0xFFFFffffULL);
	if (ret)
		return ret;

	val32 = bpf_get_prandom_u32();
	val16 = val32 & 0xFFFF;
	val8 = val32 & 0xFF;
	ret = bpf_kfunc_call_test5(val8, val16, val32);
	if (ret)
		return ret;

	/*
	 * Test multiplication with different operand sizes:
	 *
	 * val8 * 0xFF:
	 *   - Both operands promote to int (32-bit signed)
	 *   - Result: 32-bit multiplication, truncated to u8, then zero-extended
	 *
	 * val16 * 0xFFFF:
	 *   - Both operands promote to int (32-bit signed)
	 *   - Result: 32-bit multiplication, truncated to u16, then zero-extended
	 *
	 * val32 * 0xFFFFffffULL:
	 *   - val32 (u32) promotes to unsigned long long (due to ULL suffix)
	 *   - Result: 64-bit unsigned multiplication, truncated to u32, then zero-extended
	 */
	ret = bpf_kfunc_call_test5(val8 * 0xFF, val16 * 0xFFFF, val32 * 0xFFFFffffULL);
	if (ret)
		return ret;

	return 0;
}

/*
 * Assembly version testing the multiplication edge case explicitly.
 * This ensures consistent testing across different ISA versions.
 */
SEC("tc")
__naked int kfunc_call_test5_asm(void)
{
	asm volatile (
		/* Get a random u32 value */
		"call %[bpf_get_prandom_u32];"
		"r6 = r0;"              /* Save val32 in r6 */

		/* Prepare first argument: val8 * 0xFF */
		"r1 = r6;"
		"r1 &= 0xFF;"           /* val8 = val32 & 0xFF */
		"r7 = 0xFF;"
		"r1 *= r7;"             /* 64-bit mult: r1 = r1 * r7 */

		/* Prepare second argument: val16 * 0xFFFF */
		"r2 = r6;"
		"r2 &= 0xFFFF;"         /* val16 = val32 & 0xFFFF */
		"r7 = 0xFFFF;"
		"r2 *= r7;"             /* 64-bit mult: r2 = r2 * r7 */

		/* Prepare third argument: val32 * 0xFFFFffff */
		"r3 = r6;"              /* val32 */
		"r7 = 0xFFFFffff;"
		"r3 *= r7;"             /* 64-bit mult: r3 = r3 * r7 */

		/* Call kfunc with multiplication results */
		"call bpf_kfunc_call_test5;"

		/* Check return value */
		"if r0 != 0 goto exit_%=;"
		"r0 = 0;"
		"exit_%=: exit;"
		:
		: __imm(bpf_get_prandom_u32)
		: __clobber_all);
}

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

struct ctx_val {
	struct bpf_testmod_ctx __kptr *ctx;
};

struct {
	__uint(type, BPF_MAP_TYPE_ARRAY);
	__uint(max_entries, 1);
	__type(key, int);
	__type(value, struct ctx_val);
} ctx_map SEC(".maps");

SEC("tc")
int kfunc_call_ctx(struct __sk_buff *skb)
{
	struct bpf_testmod_ctx *ctx;
	int err = 0;

	ctx = bpf_testmod_ctx_create(&err);
	if (!ctx && !err)
		err = -1;
	if (ctx) {
		int key = 0;
		struct ctx_val *ctx_val = bpf_map_lookup_elem(&ctx_map, &key);

		/* Transfer ctx to map to be freed via implicit dtor call
		 * on cleanup.
		 */
		if (ctx_val)
			ctx = bpf_kptr_xchg(&ctx_val->ctx, ctx);
		if (ctx) {
			bpf_testmod_ctx_release(ctx);
			err = -1;
		}
	}
	return err;
}

char _license[] SEC("license") = "GPL";
