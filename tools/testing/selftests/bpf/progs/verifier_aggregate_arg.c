// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2026 Meta Platforms, Inc. and affiliates. */
#include <vmlinux.h>
#include <bpf/bpf_helpers.h>
#include "bpf_misc.h"

#define MIX_A	0xdeadbeefcafef00dULL
#define MIX_B	0x0123456789abcdefULL

struct pair {
	__u64 lo;
	__u64 hi;
};

struct small {
	__u32 a;
	__u32 b;
};

union upair {
	__u64 halves[2];
	struct {
		__u64 lo;
		__u64 hi;
	} parts;
};

struct with_ptr {
	void *p;
	__u64 x;
};

static __noinline __u64 take_pair(int a, struct pair p, int c)
{
	return (__u64)a + p.lo + p.hi + c;
}

SEC("tc")
__success __retval(0)
int aggregate_arg_static_struct_c_test(struct __sk_buff *skb)
{
	__u64 a = skb->len ^ MIX_A;
	__u64 b = skb->len ^ MIX_B;
	struct pair p = { .lo = a, .hi = b };

	if (take_pair(1, p, 2) != a + b + 3)
		return 1;

	return 0;
}

#if defined(__clang__)

__noinline __u64 take_pair_global(int a, struct pair p, int c)
{
	return (__u64)a + p.lo + p.hi + c;
}

SEC("tc")
__success __retval(0)
int aggregate_arg_global_struct_c_test(struct __sk_buff *skb)
{
	__u64 a = skb->len ^ MIX_A;
	__u64 b = skb->len ^ MIX_B;
	struct pair p = { .lo = a, .hi = b };

	if (take_pair_global(1, p, 2) != a + b + 3)
		return 1;

	return 0;
}

__noinline __u64 take_two_pairs_global(struct pair p, struct pair q, int d)
{
	return p.lo + p.hi + q.lo + q.hi + d;
}

SEC("tc")
__success __retval(0)
int aggregate_arg_two_structs_c_test(struct __sk_buff *skb)
{
	__u64 a = skb->len ^ MIX_A;
	__u64 b = skb->len ^ MIX_B;
	struct pair p = { .lo = a, .hi = b };
	struct pair q = { .lo = a + 1, .hi = b + 2 };

	if (take_two_pairs_global(p, q, 3) != 2 * a + 2 * b + 6)
		return 1;

	return 0;
}

__noinline __u64 take_small_global(int a, struct small s, int c)
{
	return (__u64)a + s.a + s.b + c;
}

SEC("tc")
__success __retval(0)
int aggregate_arg_small_struct_c_test(struct __sk_buff *skb)
{
	__u32 a = skb->len ^ (__u32)MIX_A;
	__u32 b = skb->len ^ (__u32)MIX_B;
	struct small s = { .a = a, .b = b };

	if (take_small_global(1, s, 2) != (__u64)a + b + 3)
		return 1;

	return 0;
}

__noinline __u64 take_upair_global(int a, union upair u, int c)
{
	return (__u64)a + u.parts.lo + u.parts.hi + c;
}

SEC("tc")
__success __retval(0)
int aggregate_arg_union_c_test(struct __sk_buff *skb)
{
	__u64 a = skb->len ^ MIX_A;
	__u64 b = skb->len ^ MIX_B;
	union upair u;

	u.halves[0] = a;
	u.halves[1] = b;
	if (take_upair_global(1, u, 2) != a + b + 3)
		return 1;

	return 0;
}

#endif

#ifdef __SIZEOF_INT128__

typedef unsigned __int128 u128;

__noinline __u64 take_i128_global(int a, u128 v, int c)
{
	return (__u64)a + (__u64)(v >> 64) + (__u64)v + c;
}

SEC("tc")
__success __retval(0)
int aggregate_arg_int128_c_test(struct __sk_buff *skb)
{
	__u64 a = skb->len ^ MIX_A;
	__u64 b = skb->len ^ MIX_B;
	u128 v = ((u128)a << 64) | b;

	if (take_i128_global(1, v, 2) != a + b + 3)
		return 1;

	return 0;
}

#endif /* __SIZEOF_INT128__ */

#if defined(__BPF_FEATURE_STACK_ARGUMENT)

static __noinline __u64 take_spilled_pair(int a, int b, int c, int d, struct pair p)
{
	return (__u64)a + b + c + d + p.lo + p.hi;
}

SEC("tc")
__arch_x86_64 __arch_arm64
__load_if_JITed()
__success __retval(0)
int aggregate_arg_spilled_struct_c_test(struct __sk_buff *skb)
{
	__u64 a = skb->len ^ MIX_A;
	__u64 b = skb->len ^ MIX_B;
	struct pair p = { .lo = a, .hi = b };
	int n = skb->len;

	if (take_spilled_pair(n, n + 1, n + 2, n + 3, p) != a + b + 4 * n + 6)
		return 1;

	return 0;
}

#endif

__noinline __u64 take_with_ptr_global(struct with_ptr s)
{
	return s.x;
}

SEC("tc")
__failure __msg("type STRUCT in take_with_ptr_global() is not composed of scalars")
int aggregate_arg_ptr_member_fail(struct __sk_buff *skb)
{
	struct with_ptr s = { .p = skb, .x = skb->len };

	return take_with_ptr_global(s);
}

char _license[] SEC("license") = "GPL";
