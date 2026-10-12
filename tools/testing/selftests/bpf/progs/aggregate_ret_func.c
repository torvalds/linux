// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2026 Meta Platforms, Inc. and affiliates. */
#include <linux/bpf.h>
#include <bpf/bpf_helpers.h>
#include <bpf_arena_common.h>
#include "bpf_misc.h"

typedef unsigned __int128 u128;

__naked u128 global_agg_good(void)
{
	asm volatile (
	"r0 = 0x1234;"	/* low 64 bits */
	"r2 = 0x5678;"	/* high 64 bits */
	"exit;"
	);
}

__naked u128 global_agg_bad(void)
{
	asm volatile (
	"r0 = 0;"
	"exit;"
	);
}

__naked u128 global_agg_bad_ptr(void)
{
	asm volatile (
	"r0 = 0;"
	"r2 = r10;"
	"exit;"
	);
}

SEC("tc")
__failure __msg("R2 !read_ok")
__naked int aggregate_ret_global_fail(void)
{
	asm volatile (
	"call %[global_agg_bad];"
	"r0 = r2;"
	"exit;"
	:
	: __imm(global_agg_bad)
	: __clobber_all);
}

SEC("tc")
__load_if_JITed()
__failure __msg("At subprogram exit the register R2 is not a scalar value")
__naked int aggregate_ret_global_ptr_fail(void)
{
	asm volatile (
	"call %[global_agg_bad_ptr];"
	"r0 = r2;"
	"exit;"
	:
	: __imm(global_agg_bad_ptr)
	: __clobber_all);
}

static __naked __noinline u128 static_agg_bad_ptr(void)
{
	asm volatile (
	"r0 = 0;"
	"r2 = r10;"	/* stack pointer placed in the second return register */
	"exit;"
	);
}

/*
 * R2 is a return register once the subprogram returns a pair, so a stack
 * pointer left in it is rejected at the callee's exit exactly as one in R0
 * is: the callee frame is gone by the time the caller could use it.
 */
SEC("tc")
__load_if_JITed()
__failure __msg("cannot return stack pointer to the caller")
__naked int aggregate_ret_static_ptr_fail(void)
{
	asm volatile (
	"call %[static_agg_bad_ptr];"
	"r0 = 0;"
	"exit;"
	:
	: __imm(static_agg_bad_ptr)
	: __clobber_all);
}

static __naked __noinline u128 static_agg_no_r2(void)
{
	asm volatile (
	"r0 = 0;"
	"exit;"
	);
}

SEC("tc")
__failure __msg("R2 !read_ok")
__naked int aggregate_ret_static_uninit_fail(void)
{
	asm volatile (
	"call %[static_agg_no_r2];"
	"r0 = r2;"
	"exit;"
	:
	: __imm(static_agg_no_r2)
	: __clobber_all);
}

static __naked __noinline u128 static_agg_precise(void)
{
	asm volatile (
	"r0 = 0;"
	"r2 = 4;"	/* second half; its value is made precise below */
	"exit;"
	);
}

SEC("tc")
__load_if_JITed()
__success __retval(0)
__log_level(2)
__msg("mark_precise: frame0: last_idx 5 first_idx 0 subseq_idx -1")
__msg("mark_precise: frame0: regs=r6 stack= before 4: (07) r1 += -8")
__msg("mark_precise: frame0: regs=r6 stack= before 3: (bf) r1 = r10")
__msg("mark_precise: frame0: regs=r6 stack= before 2: (57) r6 &= 7")
__msg("mark_precise: frame0: regs=r6 stack= before 1: (bf) r6 = r2")
__msg("mark_precise: frame0: regs=r2 stack= before 12: (95) exit")
__msg("mark_precise: frame1: regs=r2 stack= before 11: (b7) r2 = 4")
__naked int aggregate_ret_static_precise(void)
{
	asm volatile (
	"call %[static_agg_precise];"
	"r6 = r2;"		/* derived from the aggregate's second half */
	"r6 &= 7;"		/* keep it in [0, 7] to index the stack */
	"r1 = r10;"
	"r1 += -8;"
	"r1 += r6;"		/* ptr += scalar marks r6 (hence R2) precise */
	"r0 = 0;"
	"*(u8 *)(r1 + 0) = r0;"
	"r0 = 0;"
	"exit;"
	:
	: __imm(static_agg_precise)
	: __clobber_all);
}

SEC("tc")
__load_if_JITed()
__success __retval(0)
__log_level(2)
__msg("mark_precise: frame0: last_idx 5 first_idx 0 subseq_idx -1")
__msg("mark_precise: frame0: regs=r6 stack= before 4: (07) r1 += -8")
__msg("mark_precise: frame0: regs=r6 stack= before 3: (bf) r1 = r10")
__msg("mark_precise: frame0: regs=r6 stack= before 2: (57) r6 &= 7")
__msg("mark_precise: frame0: regs=r6 stack= before 1: (bf) r6 = r2")
__msg("mark_precise: frame0: regs=r2 stack= before 0: (85) call pc+9")
__naked int aggregate_ret_global_precise(void)
{
	asm volatile (
	"call %[global_agg_good];"
	"r6 = r2;"		/* derived from the aggregate's second half */
	"r6 &= 7;"		/* keep it in [0, 7] to index the stack */
	"r1 = r10;"
	"r1 += -8;"
	"r1 += r6;"		/* ptr += scalar marks r6 (hence R2) precise */
	"r0 = 0;"
	"*(u8 *)(r1 + 0) = r0;"
	"r0 = 0;"
	"exit;"
	:
	: __imm(global_agg_good)
	: __clobber_all);
}

#if defined(__clang_major__) && __clang_major__ >= 23

/* A by-value struct that smuggles a pointer, which must be rejected. */
struct with_ptr {
	void *p;
	__u64 x;
};

/* A by-value union that smuggles a pointer, which must be rejected too. */
union upair_with_ptr {
	void *p;
	__u64 halves[2];
};

__naked struct with_ptr global_ret_struct_ptr(void)
{
	asm volatile (
	"r0 = 0;"
	"r2 = 0;"
	"exit;"
	);
}

SEC("tc")
__failure __msg("Global function global_ret_struct_ptr() has unsupported return type")
__naked int aggregate_ret_global_struct_ptr_fail(void)
{
	asm volatile (
	"call %[global_ret_struct_ptr];"
	"r0 = 0;"
	"exit;"
	:
	: __imm(global_ret_struct_ptr)
	: __clobber_all);
}

__naked union upair_with_ptr global_ret_union_ptr(void)
{
	asm volatile (
	"r0 = 0;"
	"r2 = 0;"
	"exit;"
	);
}

SEC("tc")
__failure __msg("Global function global_ret_union_ptr() has unsupported return type")
__naked int aggregate_ret_global_union_ptr_fail(void)
{
	asm volatile (
	"call %[global_ret_union_ptr];"
	"r0 = 0;"
	"exit;"
	:
	: __imm(global_ret_union_ptr)
	: __clobber_all);
}

#endif

/*
 * gcc returns a by-value struct through a hidden pointer, and emits the
 * 'r0 = r1' returning it after the __naked body's exit, leaving the
 * subprogram falling through. Build these with clang only.
 */
#if defined(__clang__)

struct arena_pair {
	void __arena *lo;
	void __arena *hi;
};

struct arena_and_scalar {
	void __arena *p;
	__u64 x;
};

struct arena_array {
	void __arena *p[2];
};

struct arena_single {
	void __arena *p;
};

union arena_upair {
	void __arena *p;
	__u64 halves[2];
};

__naked struct arena_pair global_ret_arena_pair(void)
{
	asm volatile (
	"r0 = 0;"
	"r2 = 0;"
	"exit;"
	);
}

SEC("tc")
__load_if_JITed()
__success __retval(0)
__naked int aggregate_ret_global_arena_pair(void)
{
	asm volatile (
	"call %[global_ret_arena_pair];"
	"r0 = 0;"
	"exit;"
	:
	: __imm(global_ret_arena_pair)
	: __clobber_all);
}

__naked struct arena_and_scalar global_ret_arena_and_scalar(void)
{
	asm volatile (
	"r0 = 0;"
	"r2 = 0;"
	"exit;"
	);
}

SEC("tc")
__load_if_JITed()
__success __retval(0)
__naked int aggregate_ret_global_arena_and_scalar(void)
{
	asm volatile (
	"call %[global_ret_arena_and_scalar];"
	"r0 = 0;"
	"exit;"
	:
	: __imm(global_ret_arena_and_scalar)
	: __clobber_all);
}

__naked struct arena_array global_ret_arena_array(void)
{
	asm volatile (
	"r0 = 0;"
	"r2 = 0;"
	"exit;"
	);
}

SEC("tc")
__load_if_JITed()
__success __retval(0)
__naked int aggregate_ret_global_arena_array(void)
{
	asm volatile (
	"call %[global_ret_arena_array];"
	"r0 = 0;"
	"exit;"
	:
	: __imm(global_ret_arena_array)
	: __clobber_all);
}

__naked struct arena_single global_ret_arena_single(void)
{
	asm volatile (
	"r0 = 0;"
	"exit;"
	);
}

SEC("tc")
__success __retval(0)
__naked int aggregate_ret_global_arena_single(void)
{
	asm volatile (
	"call %[global_ret_arena_single];"
	"r0 = 0;"
	"exit;"
	:
	: __imm(global_ret_arena_single)
	: __clobber_all);
}

__naked union arena_upair global_ret_arena_union(void)
{
	asm volatile (
	"r0 = 0;"
	"r2 = 0;"
	"exit;"
	);
}

SEC("tc")
__load_if_JITed()
__success __retval(0)
__naked int aggregate_ret_global_arena_union(void)
{
	asm volatile (
	"call %[global_ret_arena_union];"
	"r0 = 0;"
	"exit;"
	:
	: __imm(global_ret_arena_union)
	: __clobber_all);
}

#endif

char _license[] SEC("license") = "GPL";
