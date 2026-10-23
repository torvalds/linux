// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2026 Meta Platforms, Inc. and affiliates. */
#include <vmlinux.h>
#include <bpf/bpf_helpers.h>
#include "bpf_misc.h"

#ifdef __SIZEOF_INT128__
typedef unsigned __int128 u128;
#endif

struct pair {
	__u64 lo;
	__u64 hi;
};

struct too_big {
	__u64 a;
	__u64 b;
	__u64 c;
};

#if defined(__clang__)

__noinline __u64 global_arg_pair(int a, struct pair p, int c)
{
	return (__u64)a + p.lo + p.hi + c;
}

SEC("tc")
__success __retval(0x33)
__naked int aggregate_arg_pair_asm(void)
{
	asm volatile (
	"r1 = 1;"
	"r2 = 0x10;"	/* p.lo */
	"r3 = 0x20;"	/* p.hi */
	"r4 = 2;"
	"call %[global_arg_pair];"
	"exit;"
	:
	: __imm(global_arg_pair)
	: __clobber_all);
}

SEC("tc")
__failure __msg("R2 type=fp expected=scalar")
__naked int aggregate_arg_pair_ptr_fail(void)
{
	asm volatile (
	"r1 = 1;"
	"r2 = r10;"	/* a stack pointer where p.lo belongs */
	"r3 = 0x20;"
	"r4 = 2;"
	"call %[global_arg_pair];"
	"exit;"
	:
	: __imm(global_arg_pair)
	: __clobber_all);
}

#endif

__noinline __u64 global_arg_too_big(struct too_big s)
{
	return s.a + s.b + s.c;
}

SEC("tc")
__failure __msg("in global_arg_too_big() has size 24, only 1 to 16 bytes can be passed by value")
__naked int aggregate_arg_too_big_fail(void)
{
	asm volatile (
	"r1 = 0;"
	"r2 = 0;"
	"r3 = 0;"
	"call %[global_arg_too_big];"
	"r0 = 0;"
	"exit;"
	:
	: __imm(global_arg_too_big)
	: __clobber_all);
}

#if defined(__BPF_FEATURE_STACK_ARGUMENT)

/*
 * One slot per parameter, so the slot count is settled before the parameters
 * are walked. The cases below reach the same count only once they have been.
 */
__noinline __u64 global_arg_six_scalars(int a, int b, int c, int d, int e, int f)
{
	return (__u64)a + b + c + d + e + f;
}

SEC("tc")
__failure __msg("global function global_arg_six_scalars() needs 6 > 5 argument slots")
__naked int aggregate_arg_six_scalars_fail(void)
{
	asm volatile (
	"r1 = 0;"
	"r2 = 0;"
	"r3 = 0;"
	"r4 = 0;"
	"r5 = 0;"
	"call %[global_arg_six_scalars];"
	"r0 = 0;"
	"exit;"
	:
	: __imm(global_arg_six_scalars)
	: __clobber_all);
}

__noinline __u64 global_arg_split(int a, int b, int c, int d, struct pair p)
{
	return (__u64)a + b + c + d + p.lo + p.hi;
}

SEC("tc")
__failure __msg("global function global_arg_split() needs 6 > 5 argument slots")
__naked int aggregate_arg_split_fail(void)
{
	asm volatile (
	"r1 = 0;"
	"r2 = 0;"
	"r3 = 0;"
	"r4 = 0;"
	"r5 = 0;"
	"call %[global_arg_split];"
	"r0 = 0;"
	"exit;"
	:
	: __imm(global_arg_split)
	: __clobber_all);
}

__noinline __u64 global_arg_past_regs(struct pair p, struct pair q, int a, struct pair r)
{
	return p.lo + p.hi + q.lo + q.hi + a + r.lo + r.hi;
}

SEC("tc")
__failure __msg("global function global_arg_past_regs() needs 7 > 5 argument slots")
__naked int aggregate_arg_past_regs_fail(void)
{
	asm volatile (
	"r1 = 0;"
	"r2 = 0;"
	"r3 = 0;"
	"r4 = 0;"
	"r5 = 0;"
	"call %[global_arg_past_regs];"
	"r0 = 0;"
	"exit;"
	:
	: __imm(global_arg_past_regs)
	: __clobber_all);
}

#ifdef __SIZEOF_INT128__

__noinline __u64 global_arg_i128_slots(u128 v, int a, int b, int c, int d)
{
	return (__u64)v + a + b + c + d;
}

SEC("tc")
__failure __msg("global function global_arg_i128_slots() needs 6 > 5 argument slots")
__naked int aggregate_arg_i128_slots_fail(void)
{
	asm volatile (
	"r1 = 0;"
	"r2 = 0;"
	"r3 = 0;"
	"r4 = 0;"
	"r5 = 0;"
	"call %[global_arg_i128_slots];"
	"r0 = 0;"
	"exit;"
	:
	: __imm(global_arg_i128_slots)
	: __clobber_all);
}

#endif /* __SIZEOF_INT128__ */

#endif

char _license[] SEC("license") = "GPL";
