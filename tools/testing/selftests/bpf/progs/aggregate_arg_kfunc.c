// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2026 Meta Platforms, Inc. and affiliates. */
#include <vmlinux.h>
#include <bpf/bpf_helpers.h>
#include "../test_kmods/bpf_testmod_kfunc.h"
#include "bpf_misc.h"

#ifdef __SIZEOF_INT128__
typedef unsigned __int128 u128;
#endif

#define MIX_A	0xdeadbeefcafef00dULL
#define MIX_B	0x0123456789abcdefULL

#if defined(__clang__)

SEC("tc")
__arch_x86_64 __arch_arm64
__load_if_JITed()
__success __retval(0)
int aggregate_arg_kfunc_struct(struct __sk_buff *skb)
{
	__u64 a = skb->len ^ MIX_A;
	__u64 b = skb->len ^ MIX_B;
	struct prog_test_pair_arg s = { .lo = a, .hi = b };

	if (bpf_kfunc_call_test_pair_arg(1, s, 2) != 2 * a + 3 * b + 9)
		return 1;

	return 0;
}

#endif

#ifdef __SIZEOF_INT128__

SEC("tc")
__arch_x86_64 __arch_arm64
__load_if_JITed()
__success __retval(0)
int aggregate_arg_kfunc_int128(struct __sk_buff *skb)
{
	__u64 a = skb->len ^ MIX_A;
	__u64 b = skb->len ^ MIX_B;
	u128 v = ((u128)a << 64) | b;

	if (bpf_kfunc_call_test_i128_arg(1, 2, v) != 4 * a + 3 * b + 5)
		return 1;

	return 0;
}

/*
 * arm64 rounds the register number up to an even one for an argument
 * aligned to 16 bytes, so it wants this __int128 in x2 and x3, leaving x1
 * empty, and the last argument in x4. The x86-64 ABI has no such rule.
 */
SEC("tc")
__arch_x86_64 __arch_arm64
__load_if_JITed()
__success __retval(0)
int aggregate_arg_kfunc_int128_odd(struct __sk_buff *skb)
{
	__u64 a = skb->len ^ MIX_A;
	__u64 b = skb->len ^ MIX_B;
	u128 v = ((u128)a << 64) | b;

	if (bpf_kfunc_call_test_i128_arg_odd(1, v, 2) != 1 + 2 * b + 3 * a + 4 * 2)
		return 1;

	return 0;
}

#endif /* __SIZEOF_INT128__ */

#if defined(__clang__) && defined(__BPF_FEATURE_STACK_ARGUMENT)

SEC("tc")
__arch_x86_64 __arch_arm64
__load_if_JITed()
__success __retval(0)
int aggregate_arg_kfunc_last_regs(struct __sk_buff *skb)
{
	__u64 a = skb->len ^ MIX_A;
	__u64 b = skb->len ^ MIX_B;
	struct prog_test_pair_arg s = { .lo = a, .hi = b };

	if (bpf_kfunc_call_test_pair_arg_nofit(1, 2, 3, 4, s) != 5 * a + 6 * b + 30)
		return 1;

	return 0;
}

/*
 * The x86-64 ABI moves an argument its six remaining registers cannot hold
 * wholly onto the stack. arm64, with eight argument registers, still has a
 * pair for it.
 */
SEC("tc")
__arch_x86_64 __arch_arm64
__load_if_JITed()
__success __retval(0)
int aggregate_arg_kfunc_straddle(struct __sk_buff *skb)
{
	__u64 a = skb->len ^ MIX_A;
	__u64 b = skb->len ^ MIX_B;
	struct prog_test_big_arg s = { .a = a, .b = b };

	if (bpf_kfunc_call_stack_arg_big(1, 2, 3, 4, 5, s) != 6 * a + 7 * b + 55)
		return 1;

	return 0;
}

/* The same, with an argument after the struct to take the eighth register. */
SEC("tc")
__arch_x86_64 __arch_arm64
__load_if_JITed()
__success __retval(0)
int aggregate_arg_kfunc_tail(struct __sk_buff *skb)
{
	__u64 a = skb->len ^ MIX_A;
	__u64 b = skb->len ^ MIX_B;
	struct prog_test_pair_arg s = { .lo = a, .hi = b };

	if (bpf_kfunc_call_test_pair_arg_tail(1, 2, 3, 4, 5, s, 6) != 6 * a + 7 * b + 103)
		return 1;

	return 0;
}

/*
 * arm64 gives no register to an argument its eight registers cannot hold,
 * nor to anything after it. Past its six registers the x86-64 ABI has both
 * eightbytes on the stack either way.
 */
SEC("tc")
__arch_x86_64 __arch_arm64
__load_if_JITed()
__success __retval(0)
int aggregate_arg_kfunc_split8(struct __sk_buff *skb)
{
	__u64 a = skb->len ^ MIX_A;
	__u64 b = skb->len ^ MIX_B;
	struct prog_test_pair_arg s = { .lo = a, .hi = b };

	if (bpf_kfunc_call_test_pair_arg_split8(1, 2, 3, 4, 5, 6, 7, s) != 8 * a + 9 * b + 140)
		return 1;

	return 0;
}

#ifdef __SIZEOF_INT128__

/*
 * The hole of aggregate_arg_kfunc_int128_odd, with arguments after it that
 * arm64 shifts through x5 to x7, where its JIT keeps the BPF stack slots,
 * and onto its stack. The second __int128 then leaves the second arm64
 * stack slot empty. x86-64 packs the eightbytes like the BPF convention,
 * so nothing moves there.
 */
SEC("tc")
__arch_x86_64 __arch_arm64
__load_if_JITed()
__success __retval(0)
int aggregate_arg_kfunc_int128_odd_many(struct __sk_buff *skb)
{
	__u64 a = skb->len ^ MIX_A;
	__u64 b = skb->len ^ MIX_B;
	u128 v = ((u128)a << 64) | b;
	u128 w = ((u128)~a << 64) | ~b;

	if (bpf_kfunc_call_test_i128_arg_odd_many(1, v, 2, 3, 4, 5, 6, w) !=
	    1 + 2 * b + 3 * a + 4 * 2 + 5 * 3 + 6 * 4 + 7 * 5 + 8 * 6 +
	    9 * ~b + 10 * ~a)
		return 1;

	return 0;
}

/*
 * x86-64 moves s to the stack and gives R9 back to the seventh argument.
 * w, aligned to 16 bytes by the __int128 it holds, then leaves a stack slot
 * empty before it: the fourth on x86-64, the second on arm64.
 */
SEC("tc")
__arch_x86_64 __arch_arm64
__load_if_JITed()
__success __retval(0)
int aggregate_arg_kfunc_int128_stack_hole(struct __sk_buff *skb)
{
	__u64 a = skb->len ^ MIX_A;
	__u64 b = skb->len ^ MIX_B;
	struct prog_test_pair_arg s = { .lo = a, .hi = b };
	struct prog_test_i128_arg w = { .v = ((u128)~a << 64) | ~b };

	if (bpf_kfunc_call_test_i128_arg_stack_hole(1, 2, 3, 4, 5, s, 6, 7, w) !=
	    1 + 2 * 2 + 3 * 3 + 4 * 4 + 5 * 5 + 6 * a + 7 * b + 8 * 6 + 9 * 7 +
	    10 * ~b + 11 * ~a)
		return 1;

	return 0;
}

/*
 * Both conventions pad the stack to align this __int128, and the BPF
 * convention pads for neither, so both JITs move it up an eightbyte.
 */
SEC("tc")
__arch_x86_64 __arch_arm64
__load_if_JITed()
__success __retval(0)
int aggregate_arg_kfunc_int128_pad(struct __sk_buff *skb)
{
	__u64 a = skb->len ^ MIX_A;
	__u64 b = skb->len ^ MIX_B;
	u128 v = ((u128)a << 64) | b;

	if (bpf_kfunc_call_test_i128_arg_pad(1, 2, 3, 4, 5, 6, 7, v) != 9 * a + 8 * b + 140)
		return 1;

	return 0;
}

#endif /* __SIZEOF_INT128__ */

#endif

SEC("tc")
__arch_x86_64 __arch_arm64
__failure __msg("R1 type STRUCT is not composed of scalars")
int aggregate_arg_kfunc_ptr_member(struct __sk_buff *skb)
{
	struct prog_test_ptr_arg s = { .p = skb, .x = 1 };

	return bpf_kfunc_call_test_ptr_arg(s);
}

char _license[] SEC("license") = "GPL";
