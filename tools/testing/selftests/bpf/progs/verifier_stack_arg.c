// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2026 Meta Platforms, Inc. and affiliates. */

#include <linux/bpf.h>
#include <bpf/bpf_helpers.h>
#include "bpf_misc.h"

struct {
	__uint(type, BPF_MAP_TYPE_HASH);
	__uint(max_entries, 1);
	__type(key, long long);
	__type(value, long long);
} map_hash_8b SEC(".maps");

#if (defined(__TARGET_ARCH_x86) || defined(__TARGET_ARCH_arm64)) && \
	defined(__BPF_FEATURE_STACK_ARGUMENT)

__noinline __used
static int subprog_6args(int a, int b, int c, int d, int e, int f)
{
	return a + b + c + d + e + f;
}

__noinline __used
static int subprog_7args(int a, int b, int c, int d, int e, int f, int g)
{
	return a + b + c + d + e + f + g;
}

__noinline __used
static long subprog_deref_arg6(long a, long b, long c, long d, long e, long *f)
{
	return *f;
}

SEC("tc")
__description("stack_arg: subprog with 6 args")
__success __retval(21)
__naked void stack_arg_6args(void)
{
	asm volatile (
		"r1 = 1;"
		"r2 = 2;"
		"r3 = 3;"
		"r4 = 4;"
		"r5 = 5;"
		"*(u64 *)(r11 - 8) = 6;"
		"call subprog_6args;"
		"exit;"
		::: __clobber_all
	);
}

SEC("tc")
__description("stack_arg: two subprogs with >5 args")
__success __retval(90)
__naked void stack_arg_two_subprogs(void)
{
	asm volatile (
		"r1 = 1;"
		"r2 = 2;"
		"r3 = 3;"
		"r4 = 4;"
		"r5 = 5;"
		"*(u64 *)(r11 - 8) = 10;"
		"call subprog_6args;"
		"r6 = r0;"
		"r1 = 1;"
		"r2 = 2;"
		"r3 = 3;"
		"r4 = 4;"
		"r5 = 5;"
		"*(u64 *)(r11 - 16) = 30;"
		"*(u64 *)(r11 - 8) = 20;"
		"call subprog_7args;"
		"r0 += r6;"
		"exit;"
		::: __clobber_all
	);
}

SEC("tc")
__description("stack_arg: read from uninitialized stack arg slot")
__failure
__msg("invalid read from stack arg off 8 depth 0")
__naked void stack_arg_read_uninitialized(void)
{
	asm volatile (
		"r0 = *(u64 *)(r11 + 8);"
		"r0 = 0;"
		"exit;"
		::: __clobber_all
	);
}

SEC("tc")
__description("stack_arg: gap at offset -8, only wrote -16")
__failure
__msg("callee expects 7 args, stack arg1 is not initialized")
__naked void stack_arg_gap_at_minus8(void)
{
	asm volatile (
		"r1 = 1;"
		"r2 = 2;"
		"r3 = 3;"
		"r4 = 4;"
		"r5 = 5;"
		"*(u64 *)(r11 - 16) = 30;"
		"call subprog_7args;"
		"exit;"
		::: __clobber_all
	);
}

SEC("tc")
__description("stack_arg: pruning with different stack arg types")
__failure __log_level(2)
__flag(BPF_F_TEST_STATE_FREQ)
__msg("arg JOIN insn 9 -> 10 r1: fp0-8 + _ => fp0-8|fp0+0")
__msg("arg JOIN insn 9 -> 10 sa0: fp0-8 + _ => fp0-8|fp0+0")
__msg("R{{[0-9]}} invalid mem access 'scalar'")
__naked void stack_arg_pruning_type_mismatch(void)
{
	asm volatile (
		"call %[bpf_get_prandom_u32];"
		"r6 = r0;"
		/* local = 0 on program stack */
		"r7 = 0;"
		"*(u64 *)(r10 - 8) = r7;"
		/* Branch based on random value */
		"if r6 s> 3 goto l0_%=;"
		/* Path 1: store stack pointer to outgoing arg6 */
		"r1 = r10;"
		"r1 += -8;"
		"*(u64 *)(r11 - 8) = r1;"
		"goto l1_%=;"
	"l0_%=:"
		/* Path 2: store scalar to outgoing arg6 */
		"*(u64 *)(r11 - 8) = 42;"
	"l1_%=:"
		/* Call subprog that dereferences arg6 */
		"r1 = r6;"
		"r2 = 0;"
		"r3 = 0;"
		"r4 = 0;"
		"r5 = 0;"
		"call subprog_deref_arg6;"
		"exit;"
		:: __imm(bpf_get_prandom_u32)
		: __clobber_all
	);
}

SEC("tc")
__description("stack_arg: release_reference invalidates stack arg slot")
__failure
__msg("callee expects 6 args, stack arg1 is not initialized")
__naked void stack_arg_release_ref(void)
{
	asm volatile (
		"r6 = r1;"
		/* struct bpf_sock_tuple tuple = {} */
		"r2 = 0;"
		"*(u32 *)(r10 - 8) = r2;"
		"*(u64 *)(r10 - 16) = r2;"
		"*(u64 *)(r10 - 24) = r2;"
		"*(u64 *)(r10 - 32) = r2;"
		"*(u64 *)(r10 - 40) = r2;"
		"*(u64 *)(r10 - 48) = r2;"
		/* sk = bpf_sk_lookup_tcp(ctx, &tuple, sizeof(tuple), 0, 0) */
		"r1 = r6;"
		"r2 = r10;"
		"r2 += -48;"
		"r3 = %[sizeof_bpf_sock_tuple];"
		"r4 = 0;"
		"r5 = 0;"
		"call %[bpf_sk_lookup_tcp];"
		/* r0 = sk (PTR_TO_SOCK_OR_NULL) */
		"if r0 == 0 goto l0_%=;"
		/* Store sock ref to outgoing arg6 slot */
		"*(u64 *)(r11 - 8) = r0;"
		/* Release the reference — invalidates the stack arg slot */
		"r1 = r0;"
		"call %[bpf_sk_release];"
		/* Call subprog that dereferences arg6 — should fail */
		"r1 = 1;"
		"r2 = 2;"
		"r3 = 3;"
		"r4 = 4;"
		"r5 = 5;"
		"call subprog_deref_arg6;"
	"l0_%=:"
		"r0 = 0;"
		"exit;"
		:
		: __imm(bpf_sk_lookup_tcp),
		  __imm(bpf_sk_release),
		  __imm_const(sizeof_bpf_sock_tuple, sizeof(struct bpf_sock_tuple))
		: __clobber_all
	);
}

SEC("tc")
__description("stack_arg: pkt pointer in stack arg slot invalidated after pull_data")
__failure
__msg("callee expects 6 args, stack arg1 is not initialized")
__naked void stack_arg_stale_pkt_ptr(void)
{
	asm volatile (
		"r6 = r1;"
		"r7 = *(u32 *)(r6 + %[__sk_buff_data]);"
		"r8 = *(u32 *)(r6 + %[__sk_buff_data_end]);"
		/* check pkt has at least 1 byte */
		"r0 = r7;"
		"r0 += 8;"
		"if r0 > r8 goto l0_%=;"
		/* Store valid pkt pointer to outgoing arg6 slot */
		"*(u64 *)(r11 - 8) = r7;"
		/* bpf_skb_pull_data invalidates all pkt pointers */
		"r1 = r6;"
		"r2 = 0;"
		"call %[bpf_skb_pull_data];"
		/* Call subprog that dereferences arg6 — should fail */
		"r1 = 1;"
		"r2 = 2;"
		"r3 = 3;"
		"r4 = 4;"
		"r5 = 5;"
		"call subprog_deref_arg6;"
	"l0_%=:"
		"r0 = 0;"
		"exit;"
		:
		: __imm(bpf_skb_pull_data),
		  __imm_const(__sk_buff_data, offsetof(struct __sk_buff, data)),
		  __imm_const(__sk_buff_data_end, offsetof(struct __sk_buff, data_end))
		: __clobber_all
	);
}

SEC("tc")
__description("stack_arg: null propagation rejects deref on null branch")
__failure
__msg("R{{[0-9]}} invalid mem access 'scalar'")
__naked void stack_arg_null_propagation_fail(void)
{
	asm volatile (
		"r1 = 0;"
		"*(u64 *)(r10 - 8) = r1;"
		/* r0 = bpf_map_lookup_elem(&map_hash_8b, &key) */
		"r2 = r10;"
		"r2 += -8;"
		"r1 = %[map_hash_8b] ll;"
		"call %[bpf_map_lookup_elem];"
		/* Store PTR_TO_MAP_VALUE_OR_NULL to outgoing arg6 slot */
		"*(u64 *)(r11 - 8) = r0;"
		/* null check on r0 */
		"if r0 != 0 goto l0_%=;"
		/*
		 * On null branch, outgoing slot is SCALAR(0).
		 * Call subprog that dereferences arg6 — should fail.
		 */
		"r1 = 0;"
		"r2 = 0;"
		"r3 = 0;"
		"r4 = 0;"
		"r5 = 0;"
		"call subprog_deref_arg6;"
	"l0_%=:"
		"r0 = 0;"
		"exit;"
		:
		: __imm(bpf_map_lookup_elem),
		  __imm_addr(map_hash_8b)
		: __clobber_all
	);
}

SEC("tc")
__description("stack_arg: missing store on one branch")
__failure
__msg("callee expects 7 args, stack arg1 is not initialized")
__naked void stack_arg_missing_store_one_branch(void)
{
	asm volatile (
		"call %[bpf_get_prandom_u32];"
		"r1 = 1;"
		"r2 = 2;"
		"r3 = 3;"
		"r4 = 4;"
		"r5 = 5;"
		/* Write arg7 (r11-16) before branch */
		"*(u64 *)(r11 - 16) = 20;"
		"if r0 > 0 goto l0_%=;"
		/* Path 1: write arg6 and call */
		"*(u64 *)(r11 - 8) = 10;"
		"r1 = 1;"
		"r2 = 2;"
		"r3 = 3;"
		"r4 = 4;"
		"r5 = 5;"
		"call subprog_7args;"
		"goto l1_%=;"
	"l0_%=:"
		/* Path 2: missing arg6 store, call should fail */
		"r1 = 1;"
		"r2 = 2;"
		"r3 = 3;"
		"r4 = 4;"
		"r5 = 5;"
		"call subprog_7args;"
	"l1_%=:"
		"r0 = 0;"
		"exit;"
		:: __imm(bpf_get_prandom_u32)
		: __clobber_all
	);
}

SEC("tc")
__description("stack_arg: share a store for both branches")
__success __retval(0)
__naked void stack_arg_shared_store(void)
{
	asm volatile (
		"call %[bpf_get_prandom_u32];"
		"r1 = 1;"
		"r2 = 2;"
		"r3 = 3;"
		"r4 = 4;"
		"r5 = 5;"
		/* Write arg7 (r11-16) before branch */
		"*(u64 *)(r11 - 16) = 20;"
		"if r0 > 0 goto l0_%=;"
		/* Path 1: write arg6 and call */
		"*(u64 *)(r11 - 8) = 10;"
		"r1 = 1;"
		"r2 = 2;"
		"r3 = 3;"
		"r4 = 4;"
		"r5 = 5;"
		"call subprog_7args;"
		"goto l1_%=;"
	"l0_%=:"
		/* Path 2: also write arg6 and call */
		"*(u64 *)(r11 - 8) = 30;"
		"r1 = 1;"
		"r2 = 2;"
		"r3 = 3;"
		"r4 = 4;"
		"r5 = 5;"
		"call subprog_7args;"
	"l1_%=:"
		"r0 = 0;"
		"exit;"
		:: __imm(bpf_get_prandom_u32)
		: __clobber_all
	);
}

SEC("tc")
__description("stack_arg: write beyond max outgoing depth")
__failure
__msg("stack arg write offset -80 exceeds max 7 stack args")
__naked void stack_arg_write_beyond_max(void)
{
	asm volatile (
		"r1 = 1;"
		"r2 = 2;"
		"r3 = 3;"
		"r4 = 4;"
		"r5 = 5;"
		/* Write to offset -80, way beyond any callee's needs */
		"*(u64 *)(r11 - 80) = 99;"
		"*(u64 *)(r11 - 16) = 20;"
		"*(u64 *)(r11 - 8) = 10;"
		"call subprog_7args;"
		"r0 = 0;"
		"exit;"
		::: __clobber_all
	);
}

SEC("tc")
__description("stack_arg: write unused stack arg slot")
__failure
__msg("func#0 writes 5 stack arg slots, but calls only require 2")
__naked void stack_arg_write_unused_slot(void)
{
	asm volatile (
		"r1 = 1;"
		"r2 = 2;"
		"r3 = 3;"
		"r4 = 4;"
		"r5 = 5;"
		/* Write to offset -40, unused for the callee */
		"*(u64 *)(r11 - 40) = 99;"
		"*(u64 *)(r11 - 16) = 20;"
		"*(u64 *)(r11 - 8) = 10;"
		"call subprog_7args;"
		"r0 = 0;"
		"exit;"
		::: __clobber_all
	);
}

SEC("tc")
__description("stack_arg: sequential calls reuse slots")
__failure
__msg("callee expects 7 args, stack arg1 is not initialized")
__naked void stack_arg_sequential_calls(void)
{
	asm volatile (
		"r1 = 1;"
		"r2 = 2;"
		"r3 = 3;"
		"r4 = 4;"
		"r5 = 5;"
		"*(u64 *)(r11 - 8) = 6;"
		"*(u64 *)(r11 - 16) = 7;"
		"call subprog_7args;"
		"r6 = r0;"
		"r1 = 1;"
		"r2 = 2;"
		"r3 = 3;"
		"r4 = 4;"
		"r5 = 5;"
		"call subprog_7args;"
		"r0 += r6;"
		"exit;"
		::: __clobber_all
	);
}

#else

SEC("socket")
__description("stack_arg is not supported by compiler or jit, use a dummy test")
__success
int dummy_test(void)
{
	return 0;
}

#endif

char _license[] SEC("license") = "GPL";
