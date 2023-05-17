// SPDX-License-Identifier: GPL-2.0
/* Converted from tools/testing/selftests/bpf/verifier/regalloc.c */

#include <linux/bpf.h>
#include <bpf/bpf_helpers.h>
#include "bpf_misc.h"

#define MAX_ENTRIES 11

struct test_val {
	unsigned int index;
	int foo[MAX_ENTRIES];
};

struct {
	__uint(type, BPF_MAP_TYPE_HASH);
	__uint(max_entries, 1);
	__type(key, long long);
	__type(value, struct test_val);
} map_hash_48b SEC(".maps");

SEC("tracepoint")
__description("regalloc basic")
__success __flag(BPF_F_ANY_ALIGNMENT)
__naked void regalloc_basic(void)
{
	asm volatile ("					\
	r6 = r1;					\
	r1 = 0;						\
	*(u64*)(r10 - 8) = r1;				\
	r2 = r10;					\
	r2 += -8;					\
	r1 = %[map_hash_48b] ll;			\
	call %[bpf_map_lookup_elem];			\
	if r0 == 0 goto l0_%=;				\
	r7 = r0;					\
	call %[bpf_get_prandom_u32];			\
	r2 = r0;					\
	if r0 s> 20 goto l0_%=;				\
	if r2 s< 0 goto l0_%=;				\
	r7 += r0;					\
	r7 += r2;					\
	r0 = *(u64*)(r7 + 0);				\
l0_%=:	exit;						\
"	:
	: __imm(bpf_get_prandom_u32),
	  __imm(bpf_map_lookup_elem),
	  __imm_addr(map_hash_48b)
	: __clobber_all);
}

SEC("tracepoint")
__description("regalloc negative")
__failure __msg("invalid access to map value, value_size=48 off=48 size=1")
__naked void regalloc_negative(void)
{
	asm volatile ("					\
	r6 = r1;					\
	r1 = 0;						\
	*(u64*)(r10 - 8) = r1;				\
	r2 = r10;					\
	r2 += -8;					\
	r1 = %[map_hash_48b] ll;			\
	call %[bpf_map_lookup_elem];			\
	if r0 == 0 goto l0_%=;				\
	r7 = r0;					\
	call %[bpf_get_prandom_u32];			\
	r2 = r0;					\
	if r0 s> 24 goto l0_%=;				\
	if r2 s< 0 goto l0_%=;				\
	r7 += r0;					\
	r7 += r2;					\
	r0 = *(u8*)(r7 + 0);				\
l0_%=:	exit;						\
"	:
	: __imm(bpf_get_prandom_u32),
	  __imm(bpf_map_lookup_elem),
	  __imm_addr(map_hash_48b)
	: __clobber_all);
}

SEC("tracepoint")
__description("regalloc src_reg mark")
__success __flag(BPF_F_ANY_ALIGNMENT)
__naked void regalloc_src_reg_mark(void)
{
	asm volatile ("					\
	r6 = r1;					\
	r1 = 0;						\
	*(u64*)(r10 - 8) = r1;				\
	r2 = r10;					\
	r2 += -8;					\
	r1 = %[map_hash_48b] ll;			\
	call %[bpf_map_lookup_elem];			\
	if r0 == 0 goto l0_%=;				\
	r7 = r0;					\
	call %[bpf_get_prandom_u32];			\
	r2 = r0;					\
	if r0 s> 20 goto l0_%=;				\
	r3 = 0;						\
	if r3 s>= r2 goto l0_%=;			\
	r7 += r0;					\
	r7 += r2;					\
	r0 = *(u64*)(r7 + 0);				\
l0_%=:	exit;						\
"	:
	: __imm(bpf_get_prandom_u32),
	  __imm(bpf_map_lookup_elem),
	  __imm_addr(map_hash_48b)
	: __clobber_all);
}

SEC("tracepoint")
__description("regalloc src_reg negative")
__failure __msg("invalid access to map value, value_size=48 off=44 size=8")
__flag(BPF_F_ANY_ALIGNMENT)
__naked void regalloc_src_reg_negative(void)
{
	asm volatile ("					\
	r6 = r1;					\
	r1 = 0;						\
	*(u64*)(r10 - 8) = r1;				\
	r2 = r10;					\
	r2 += -8;					\
	r1 = %[map_hash_48b] ll;			\
	call %[bpf_map_lookup_elem];			\
	if r0 == 0 goto l0_%=;				\
	r7 = r0;					\
	call %[bpf_get_prandom_u32];			\
	r2 = r0;					\
	if r0 s> 22 goto l0_%=;				\
	r3 = 0;						\
	if r3 s>= r2 goto l0_%=;			\
	r7 += r0;					\
	r7 += r2;					\
	r0 = *(u64*)(r7 + 0);				\
l0_%=:	exit;						\
"	:
	: __imm(bpf_get_prandom_u32),
	  __imm(bpf_map_lookup_elem),
	  __imm_addr(map_hash_48b)
	: __clobber_all);
}

SEC("tracepoint")
__description("regalloc and spill")
__success __flag(BPF_F_ANY_ALIGNMENT)
__naked void regalloc_and_spill(void)
{
	asm volatile ("					\
	r6 = r1;					\
	r1 = 0;						\
	*(u64*)(r10 - 8) = r1;				\
	r2 = r10;					\
	r2 += -8;					\
	r1 = %[map_hash_48b] ll;			\
	call %[bpf_map_lookup_elem];			\
	if r0 == 0 goto l0_%=;				\
	r7 = r0;					\
	call %[bpf_get_prandom_u32];			\
	r2 = r0;					\
	if r0 s> 20 goto l0_%=;				\
	/* r0 has upper bound that should propagate into r2 */\
	*(u64*)(r10 - 8) = r2;		/* spill r2 */	\
	r0 = 0;						\
	r2 = 0;				/* clear r0 and r2 */\
	r3 = *(u64*)(r10 - 8);		/* fill r3 */	\
	if r0 s>= r3 goto l0_%=;			\
	/* r3 has lower and upper bounds */		\
	r7 += r3;					\
	r0 = *(u64*)(r7 + 0);				\
l0_%=:	exit;						\
"	:
	: __imm(bpf_get_prandom_u32),
	  __imm(bpf_map_lookup_elem),
	  __imm_addr(map_hash_48b)
	: __clobber_all);
}

SEC("tracepoint")
__description("regalloc and spill negative")
__failure __msg("invalid access to map value, value_size=48 off=48 size=8")
__flag(BPF_F_ANY_ALIGNMENT)
__naked void regalloc_and_spill_negative(void)
{
	asm volatile ("					\
	r6 = r1;					\
	r1 = 0;						\
	*(u64*)(r10 - 8) = r1;				\
	r2 = r10;					\
	r2 += -8;					\
	r1 = %[map_hash_48b] ll;			\
	call %[bpf_map_lookup_elem];			\
	if r0 == 0 goto l0_%=;				\
	r7 = r0;					\
	call %[bpf_get_prandom_u32];			\
	r2 = r0;					\
	if r0 s> 48 goto l0_%=;				\
	/* r0 has upper bound that should propagate into r2 */\
	*(u64*)(r10 - 8) = r2;		/* spill r2 */	\
	r0 = 0;						\
	r2 = 0;				/* clear r0 and r2 */\
	r3 = *(u64*)(r10 - 8);		/* fill r3 */\
	if r0 s>= r3 goto l0_%=;			\
	/* r3 has lower and upper bounds */		\
	r7 += r3;					\
	r0 = *(u64*)(r7 + 0);				\
l0_%=:	exit;						\
"	:
	: __imm(bpf_get_prandom_u32),
	  __imm(bpf_map_lookup_elem),
	  __imm_addr(map_hash_48b)
	: __clobber_all);
}

SEC("tracepoint")
__description("regalloc three regs")
__success __flag(BPF_F_ANY_ALIGNMENT)
__naked void regalloc_three_regs(void)
{
	asm volatile ("					\
	r6 = r1;					\
	r1 = 0;						\
	*(u64*)(r10 - 8) = r1;				\
	r2 = r10;					\
	r2 += -8;					\
	r1 = %[map_hash_48b] ll;			\
	call %[bpf_map_lookup_elem];			\
	if r0 == 0 goto l0_%=;				\
	r7 = r0;					\
	call %[bpf_get_prandom_u32];			\
	r2 = r0;					\
	r4 = r2;					\
	if r0 s> 12 goto l0_%=;				\
	if r2 s< 0 goto l0_%=;				\
	r7 += r0;					\
	r7 += r2;					\
	r7 += r4;					\
	r0 = *(u64*)(r7 + 0);				\
l0_%=:	exit;						\
"	:
	: __imm(bpf_get_prandom_u32),
	  __imm(bpf_map_lookup_elem),
	  __imm_addr(map_hash_48b)
	: __clobber_all);
}

SEC("tracepoint")
__description("regalloc after call")
__success __flag(BPF_F_ANY_ALIGNMENT)
__naked void regalloc_after_call(void)
{
	asm volatile ("					\
	r6 = r1;					\
	r1 = 0;						\
	*(u64*)(r10 - 8) = r1;				\
	r2 = r10;					\
	r2 += -8;					\
	r1 = %[map_hash_48b] ll;			\
	call %[bpf_map_lookup_elem];			\
	if r0 == 0 goto l0_%=;				\
	r7 = r0;					\
	call %[bpf_get_prandom_u32];			\
	r8 = r0;					\
	r9 = r0;					\
	call regalloc_after_call__1;			\
	if r8 s> 20 goto l0_%=;				\
	if r9 s< 0 goto l0_%=;				\
	r7 += r8;					\
	r7 += r9;					\
	r0 = *(u64*)(r7 + 0);				\
l0_%=:	exit;						\
"	:
	: __imm(bpf_get_prandom_u32),
	  __imm(bpf_map_lookup_elem),
	  __imm_addr(map_hash_48b)
	: __clobber_all);
}

static __naked __noinline __attribute__((used))
void regalloc_after_call__1(void)
{
	asm volatile ("					\
	r0 = 0;						\
	exit;						\
"	::: __clobber_all);
}

SEC("tracepoint")
__description("regalloc in callee")
__success __flag(BPF_F_ANY_ALIGNMENT)
__naked void regalloc_in_callee(void)
{
	asm volatile ("					\
	r6 = r1;					\
	r1 = 0;						\
	*(u64*)(r10 - 8) = r1;				\
	r2 = r10;					\
	r2 += -8;					\
	r1 = %[map_hash_48b] ll;			\
	call %[bpf_map_lookup_elem];			\
	if r0 == 0 goto l0_%=;				\
	r7 = r0;					\
	call %[bpf_get_prandom_u32];			\
	r1 = r0;					\
	r2 = r0;					\
	r3 = r7;					\
	call regalloc_in_callee__1;			\
l0_%=:	exit;						\
"	:
	: __imm(bpf_get_prandom_u32),
	  __imm(bpf_map_lookup_elem),
	  __imm_addr(map_hash_48b)
	: __clobber_all);
}

static __naked __noinline __attribute__((used))
void regalloc_in_callee__1(void)
{
	asm volatile ("					\
	if r1 s> 20 goto l0_%=;				\
	if r2 s< 0 goto l0_%=;				\
	r3 += r1;					\
	r3 += r2;					\
	r0 = *(u64*)(r3 + 0);				\
	exit;						\
l0_%=:	r0 = 0;						\
	exit;						\
"	::: __clobber_all);
}

SEC("tracepoint")
__description("regalloc, spill, JEQ")
__success
__naked void regalloc_spill_jeq(void)
{
	asm volatile ("					\
	r6 = r1;					\
	r1 = 0;						\
	*(u64*)(r10 - 8) = r1;				\
	r2 = r10;					\
	r2 += -8;					\
	r1 = %[map_hash_48b] ll;			\
	call %[bpf_map_lookup_elem];			\
	*(u64*)(r10 - 8) = r0;		/* spill r0 */	\
	if r0 == 0 goto l0_%=;				\
l0_%=:	/* The verifier will walk the rest twice with r0 == 0 and r0 == map_value */\
	call %[bpf_get_prandom_u32];			\
	r2 = r0;					\
	if r2 == 20 goto l1_%=;				\
l1_%=:	/* The verifier will walk the rest two more times with r0 == 20 and r0 == unknown */\
	r3 = *(u64*)(r10 - 8);		/* fill r3 with map_value */\
	if r3 == 0 goto l2_%=;		/* skip ldx if map_value == NULL */\
	/* Buggy verifier will think that r3 == 20 here */\
	r0 = *(u64*)(r3 + 0);		/* read from map_value */\
l2_%=:	exit;						\
"	:
	: __imm(bpf_get_prandom_u32),
	  __imm(bpf_map_lookup_elem),
	  __imm_addr(map_hash_48b)
	: __clobber_all);
}

char _license[] SEC("license") = "GPL";
