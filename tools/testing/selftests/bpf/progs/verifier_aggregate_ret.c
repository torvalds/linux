// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2026 Meta Platforms, Inc. and affiliates. */
#include <vmlinux.h>
#include <bpf/bpf_helpers.h>
#include "../test_kmods/bpf_testmod_kfunc.h"
#include "bpf_misc.h"

#if defined(__clang_major__) && __clang_major__ >= 23

#define MIX_A	0xdeadbeefcafef00dULL
#define MIX_B	0x0123456789abcdefULL

typedef unsigned __int128 u128;

struct pair {
	__u64 lo;	/* R0 */
	__u64 hi;	/* R2 */
};

union upair {
	__u64 halves[2];
	struct {
		__u64 lo;	/* R0 */
		__u64 hi;	/* R2 */
	} parts;
};

static __noinline u128 make_i128(__u64 a, __u64 b)
{
	return ((u128)(a + b) << 64) | (a - b);
}

SEC("tc")
__load_if_JITed()
__success __retval(0)
int aggregate_ret_int128_c_test(struct __sk_buff *skb)
{
	__u64 a = skb->len ^ MIX_A;
	__u64 b = skb->len ^ MIX_B;
	u128 v;

	v = make_i128(a, b);
	if ((__u64)(v >> 64) != a + b)
		return 1;
	if ((__u64)v != a - b)
		return 2;

	return 0;
}

static __noinline struct pair make_pair(__u64 a, __u64 b)
{
	struct pair p = { .lo = a + b, .hi = a - b };

	return p;
}

SEC("tc")
__load_if_JITed()
__success __retval(0)
int aggregate_ret_struct_c_test(struct __sk_buff *skb)
{
	__u64 a = skb->len ^ MIX_A;
	__u64 b = skb->len ^ MIX_B;
	struct pair p;

	p = make_pair(a, b);
	if (p.lo != a + b)
		return 1;
	if (p.hi != a - b)
		return 2;

	return 0;
}

__noinline struct pair make_pair_global(__u64 a, __u64 b)
{
	struct pair p = { .lo = a + b, .hi = a - b };

	return p;
}

SEC("tc")
__load_if_JITed()
__success __retval(0)
int aggregate_ret_global_struct_c_test(struct __sk_buff *skb)
{
	__u64 a = skb->len ^ MIX_A;
	__u64 b = skb->len ^ MIX_B;
	struct pair p;

	p = make_pair_global(a, b);
	if (p.lo != a + b)
		return 1;
	if (p.hi != a - b)
		return 2;

	return 0;
}

static __noinline union upair make_upair(__u64 a, __u64 b)
{
	union upair p;

	p.halves[0] = a + b;
	p.halves[1] = a - b;
	return p;
}

SEC("tc")
__load_if_JITed()
__success __retval(0)
int aggregate_ret_union_c_test(struct __sk_buff *skb)
{
	__u64 a = skb->len ^ MIX_A;
	__u64 b = skb->len ^ MIX_B;
	union upair p;

	p = make_upair(a, b);
	if (p.parts.lo != a + b)
		return 1;
	if (p.parts.hi != a - b)
		return 2;

	return 0;
}

SEC("tc")
__arch_x86_64 __arch_arm64
__load_if_JITed()
__success __retval(0)
int aggregate_ret_kfunc_int128_c_test(struct __sk_buff *skb)
{
	__u64 a = skb->len ^ MIX_A;
	__u64 b = skb->len ^ MIX_B;
	u128 v;

	v = bpf_kfunc_call_test_i128(a, b);
	if ((__u64)(v >> 64) != a + b)
		return 1;
	if ((__u64)v != a - b)
		return 2;

	return 0;
}

SEC("tc")
__arch_x86_64 __arch_arm64
__load_if_JITed()
__success __retval(0)
int aggregate_ret_kfunc_struct_c_test(struct __sk_buff *skb)
{
	__u64 a = skb->len ^ MIX_A;
	__u64 b = skb->len ^ MIX_B;
	struct prog_test_ret_pair p;

	p = bpf_kfunc_call_test_ret_pair(a, b);
	if (p.lo != a + b)
		return 1;
	if (p.hi != a - b)
		return 2;

	return 0;
}

#else

SEC("socket")
__description("verifier_aggregate_ret: needs LLVM 23, dummy test")
__skip("needs LLVM 23")
__success
int dummy_test(void)
{
	return 0;
}

#endif

char _license[] SEC("license") = "GPL";
