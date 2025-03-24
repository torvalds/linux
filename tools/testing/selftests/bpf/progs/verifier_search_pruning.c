// SPDX-License-Identifier: GPL-2.0
/* Converted from tools/testing/selftests/bpf/verifier/search_pruning.c */

#include <linux/bpf.h>
#include <../../../include/linux/filter.h>
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

struct {
	__uint(type, BPF_MAP_TYPE_HASH);
	__uint(max_entries, 1);
	__type(key, long long);
	__type(value, long long);
} map_hash_8b SEC(".maps");

SEC("socket")
__description("pointer/scalar confusion in state equality check (way 1)")
__success __failure_unpriv __msg_unpriv("R0 leaks addr as return value")
__retval(POINTER_VALUE)
__naked void state_equality_check_way_1(void)
{
	asm volatile ("					\
	r1 = 0;						\
	*(u64*)(r10 - 8) = r1;				\
	r2 = r10;					\
	r2 += -8;					\
	r1 = %[map_hash_8b] ll;				\
	call %[bpf_map_lookup_elem];			\
	if r0 == 0 goto l0_%=;				\
	r0 = *(u64*)(r0 + 0);				\
	goto l1_%=;					\
l0_%=:	r0 = r10;					\
l1_%=:	goto l2_%=;					\
l2_%=:	exit;						\
"	:
	: __imm(bpf_map_lookup_elem),
	  __imm_addr(map_hash_8b)
	: __clobber_all);
}

SEC("socket")
__description("pointer/scalar confusion in state equality check (way 2)")
__success __failure_unpriv __msg_unpriv("R0 leaks addr as return value")
__retval(POINTER_VALUE)
__naked void state_equality_check_way_2(void)
{
	asm volatile ("					\
	r1 = 0;						\
	*(u64*)(r10 - 8) = r1;				\
	r2 = r10;					\
	r2 += -8;					\
	r1 = %[map_hash_8b] ll;				\
	call %[bpf_map_lookup_elem];			\
	if r0 != 0 goto l0_%=;				\
	r0 = r10;					\
	goto l1_%=;					\
l0_%=:	r0 = *(u64*)(r0 + 0);				\
l1_%=:	exit;						\
"	:
	: __imm(bpf_map_lookup_elem),
	  __imm_addr(map_hash_8b)
	: __clobber_all);
}

SEC("lwt_in")
__description("liveness pruning and write screening")
__failure __msg("R0 !read_ok")
__naked void liveness_pruning_and_write_screening(void)
{
	asm volatile ("					\
	/* Get an unknown value */			\
	r2 = *(u32*)(r1 + 0);				\
	/* branch conditions teach us nothing about R2 */\
	if r2 >= 0 goto l0_%=;				\
	r0 = 0;						\
l0_%=:	if r2 >= 0 goto l1_%=;				\
	r0 = 0;						\
l1_%=:	exit;						\
"	::: __clobber_all);
}

SEC("socket")
__description("varlen_map_value_access pruning")
__failure __msg("R0 unbounded memory access")
__failure_unpriv __msg_unpriv("R0 leaks addr")
__flag(BPF_F_ANY_ALIGNMENT)
__naked void varlen_map_value_access_pruning(void)
{
	asm volatile ("					\
	r1 = 0;						\
	*(u64*)(r10 - 8) = r1;				\
	r2 = r10;					\
	r2 += -8;					\
	r1 = %[map_hash_48b] ll;			\
	call %[bpf_map_lookup_elem];			\
	if r0 == 0 goto l0_%=;				\
	r1 = *(u64*)(r0 + 0);				\
	w2 = %[max_entries];				\
	if r2 s> r1 goto l1_%=;				\
	w1 = 0;						\
l1_%=:	w1 <<= 2;					\
	r0 += r1;					\
	goto l2_%=;					\
l2_%=:	r1 = %[test_val_foo];				\
	*(u64*)(r0 + 0) = r1;				\
l0_%=:	exit;						\
"	:
	: __imm(bpf_map_lookup_elem),
	  __imm_addr(map_hash_48b),
	  __imm_const(max_entries, MAX_ENTRIES),
	  __imm_const(test_val_foo, offsetof(struct test_val, foo))
	: __clobber_all);
}

SEC("tracepoint")
__description("search pruning: all branches should be verified (nop operation)")
__failure __msg("R6 invalid mem access 'scalar'")
__naked void should_be_verified_nop_operation(void)
{
	asm volatile ("					\
	r2 = r10;					\
	r2 += -8;					\
	r1 = 0;						\
	*(u64*)(r2 + 0) = r1;				\
	r1 = %[map_hash_8b] ll;				\
	call %[bpf_map_lookup_elem];			\
	if r0 == 0 goto l0_%=;				\
	r3 = *(u64*)(r0 + 0);				\
	if r3 == 0xbeef goto l1_%=;			\
	r4 = 0;						\
	goto l2_%=;					\
l1_%=:	r4 = 1;						\
l2_%=:	*(u64*)(r10 - 16) = r4;				\
	call %[bpf_ktime_get_ns];			\
	r5 = *(u64*)(r10 - 16);				\
	if r5 == 0 goto l0_%=;				\
	r6 = 0;						\
	r1 = 0xdead;					\
	*(u64*)(r6 + 0) = r1;				\
l0_%=:	exit;						\
"	:
	: __imm(bpf_ktime_get_ns),
	  __imm(bpf_map_lookup_elem),
	  __imm_addr(map_hash_8b)
	: __clobber_all);
}

SEC("socket")
__description("search pruning: all branches should be verified (invalid stack access)")
/* in privileged mode reads from uninitialized stack locations are permitted */
__success __failure_unpriv
__msg_unpriv("invalid read from stack off -16+0 size 8")
__retval(0)
__naked void be_verified_invalid_stack_access(void)
{
	asm volatile ("					\
	r2 = r10;					\
	r2 += -8;					\
	r1 = 0;						\
	*(u64*)(r2 + 0) = r1;				\
	r1 = %[map_hash_8b] ll;				\
	call %[bpf_map_lookup_elem];			\
	if r0 == 0 goto l0_%=;				\
	r3 = *(u64*)(r0 + 0);				\
	r4 = 0;						\
	if r3 == 0xbeef goto l1_%=;			\
	*(u64*)(r10 - 16) = r4;				\
	goto l2_%=;					\
l1_%=:	*(u64*)(r10 - 24) = r4;				\
l2_%=:	call %[bpf_ktime_get_ns];			\
	r5 = *(u64*)(r10 - 16);				\
l0_%=:	exit;						\
"	:
	: __imm(bpf_ktime_get_ns),
	  __imm(bpf_map_lookup_elem),
	  __imm_addr(map_hash_8b)
	: __clobber_all);
}

SEC("tracepoint")
__description("precision tracking for u32 spill/fill")
__failure __msg("R0 min value is outside of the allowed memory range")
__naked void tracking_for_u32_spill_fill(void)
{
	asm volatile ("					\
	r7 = r1;					\
	call %[bpf_get_prandom_u32];			\
	w6 = 32;					\
	if r0 == 0 goto l0_%=;				\
	w6 = 4;						\
l0_%=:	/* Additional insns to introduce a pruning point. */\
	call %[bpf_get_prandom_u32];			\
	r3 = 0;						\
	r3 = 0;						\
	if r0 == 0 goto l1_%=;				\
	r3 = 0;						\
l1_%=:	/* u32 spill/fill */				\
	*(u32*)(r10 - 8) = r6;				\
	r8 = *(u32*)(r10 - 8);				\
	/* out-of-bound map value access for r6=32 */	\
	r1 = 0;						\
	*(u64*)(r10 - 16) = r1;				\
	r2 = r10;					\
	r2 += -16;					\
	r1 = %[map_hash_8b] ll;				\
	call %[bpf_map_lookup_elem];			\
	if r0 == 0 goto l2_%=;				\
	r0 += r8;					\
	r1 = *(u32*)(r0 + 0);				\
l2_%=:	r0 = 0;						\
	exit;						\
"	:
	: __imm(bpf_get_prandom_u32),
	  __imm(bpf_map_lookup_elem),
	  __imm_addr(map_hash_8b)
	: __clobber_all);
}

SEC("tracepoint")
__description("precision tracking for u32 spills, u64 fill")
__failure __msg("div by zero")
__naked void for_u32_spills_u64_fill(void)
{
	asm volatile ("					\
	call %[bpf_get_prandom_u32];			\
	r6 = r0;					\
	w7 = 0xffffffff;				\
	/* Additional insns to introduce a pruning point. */\
	r3 = 1;						\
	r3 = 1;						\
	r3 = 1;						\
	r3 = 1;						\
	call %[bpf_get_prandom_u32];			\
	if r0 == 0 goto l0_%=;				\
	r3 = 1;						\
l0_%=:	w3 /= 0;					\
	/* u32 spills, u64 fill */			\
	*(u32*)(r10 - 4) = r6;				\
	*(u32*)(r10 - 8) = r7;				\
	r8 = *(u64*)(r10 - 8);				\
	/* if r8 != X goto pc+1  r8 known in fallthrough branch */\
	if r8 != 0xffffffff goto l1_%=;			\
	r3 = 1;						\
l1_%=:	/* if r8 == X goto pc+1  condition always true on first\
	 * traversal, so starts backtracking to mark r8 as requiring\
	 * precision. r7 marked as needing precision. r6 not marked\
	 * since it's not tracked.			\
	 */						\
	if r8 == 0xffffffff goto l2_%=;			\
	/* fails if r8 correctly marked unknown after fill. */\
	w3 /= 0;					\
l2_%=:	r0 = 0;						\
	exit;						\
"	:
	: __imm(bpf_get_prandom_u32)
	: __clobber_all);
}

SEC("socket")
__description("allocated_stack")
__success __msg("processed 15 insns")
__success_unpriv __msg_unpriv("") __log_level(1) __retval(0)
__naked void allocated_stack(void)
{
	asm volatile ("					\
	r6 = r1;					\
	call %[bpf_get_prandom_u32];			\
	r7 = r0;					\
	if r0 == 0 goto l0_%=;				\
	r0 = 0;						\
	*(u64*)(r10 - 8) = r6;				\
	r6 = *(u64*)(r10 - 8);				\
	*(u8*)(r10 - 9) = r7;				\
	r7 = *(u8*)(r10 - 9);				\
l0_%=:	if r0 != 0 goto l1_%=;				\
l1_%=:	if r0 != 0 goto l2_%=;				\
l2_%=:	if r0 != 0 goto l3_%=;				\
l3_%=:	if r0 != 0 goto l4_%=;				\
l4_%=:	exit;						\
"	:
	: __imm(bpf_get_prandom_u32)
	: __clobber_all);
}

/* The test performs a conditional 64-bit write to a stack location
 * fp[-8], this is followed by an unconditional 8-bit write to fp[-8],
 * then data is read from fp[-8]. This sequence is unsafe.
 *
 * The test would be mistakenly marked as safe w/o dst register parent
 * preservation in verifier.c:copy_register_state() function.
 *
 * Note the usage of BPF_F_TEST_STATE_FREQ to force creation of the
 * checkpoint state after conditional 64-bit assignment.
 */

SEC("socket")
__description("write tracking and register parent chain bug")
/* in privileged mode reads from uninitialized stack locations are permitted */
__success __failure_unpriv
__msg_unpriv("invalid read from stack off -8+1 size 8")
__retval(0) __flag(BPF_F_TEST_STATE_FREQ)
__naked void and_register_parent_chain_bug(void)
{
	asm volatile ("					\
	/* r6 = ktime_get_ns() */			\
	call %[bpf_ktime_get_ns];			\
	r6 = r0;					\
	/* r0 = ktime_get_ns() */			\
	call %[bpf_ktime_get_ns];			\
	/* if r0 > r6 goto +1 */			\
	if r0 > r6 goto l0_%=;				\
	/* *(u64 *)(r10 - 8) = 0xdeadbeef */		\
	r0 = 0xdeadbeef;				\
	*(u64*)(r10 - 8) = r0;				\
l0_%=:	r1 = 42;					\
	*(u8*)(r10 - 8) = r1;				\
	r2 = *(u64*)(r10 - 8);				\
	/* exit(0) */					\
	r0 = 0;						\
	exit;						\
"	:
	: __imm(bpf_ktime_get_ns)
	: __clobber_all);
}

/* Without checkpoint forcibly inserted at the back-edge a loop this
 * test would take a very long time to verify.
 */
SEC("kprobe")
__failure __log_level(4)
__msg("BPF program is too large.")
__naked void short_loop1(void)
{
	asm volatile (
	"   r7 = *(u16 *)(r1 +0);"
	"1: r7 += 0x1ab064b9;"
	"   .8byte %[jset];" /* same as 'if r7 & 0x702000 goto 1b;' */
	"   r7 &= 0x1ee60e;"
	"   r7 += r1;"
	"   if r7 s> 0x37d2 goto +0;"
	"   r0 = 0;"
	"   exit;"
	:
	: __imm_insn(jset, BPF_JMP_IMM(BPF_JSET, BPF_REG_7, 0x702000, -2))
	: __clobber_all);
}

char _license[] SEC("license") = "GPL";
