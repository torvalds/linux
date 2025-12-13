// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2025 Meta Platforms, Inc. and affiliates. */

#include <linux/bpf.h>
#include <bpf/bpf_helpers.h>
#include "bpf_misc.h"

struct {
	__uint(type, BPF_MAP_TYPE_HASH);
	__uint(max_entries, 1);
	__type(key, int);
	__type(value, long long);
} map SEC(".maps");

SEC("socket")
__log_level(2)
__msg("(0) frame 0 insn 2 +written -8")
__msg("(0) frame 0 insn 1 +live -24")
__msg("(0) frame 0 insn 1 +written -8")
__msg("(0) frame 0 insn 0 +live -8,-24")
__msg("(0) frame 0 insn 0 +written -8")
__msg("(0) live stack update done in 2 iterations")
__naked void simple_read_simple_write(void)
{
	asm volatile (
	"r1 = *(u64 *)(r10 - 8);"
	"r2 = *(u64 *)(r10 - 24);"
	"*(u64 *)(r10 - 8) = r1;"
	"r0 = 0;"
	"exit;"
	::: __clobber_all);
}

SEC("socket")
__log_level(2)
__msg("(0) frame 0 insn 1 +live -8")
__not_msg("(0) frame 0 insn 1 +written")
__msg("(0) live stack update done in 2 iterations")
__msg("(0) frame 0 insn 1 +live -16")
__msg("(0) frame 0 insn 1 +written -32")
__msg("(0) live stack update done in 2 iterations")
__naked void read_write_join(void)
{
	asm volatile (
	"call %[bpf_get_prandom_u32];"
	"if r0 > 42 goto 1f;"
	"r0 = *(u64 *)(r10 - 8);"
	"*(u64 *)(r10 - 32) = r0;"
	"*(u64 *)(r10 - 40) = r0;"
	"exit;"
"1:"
	"r0 = *(u64 *)(r10 - 16);"
	"*(u64 *)(r10 - 32) = r0;"
	"exit;"
	:: __imm(bpf_get_prandom_u32)
	: __clobber_all);
}

SEC("socket")
__log_level(2)
__msg("2: (25) if r0 > 0x2a goto pc+1")
__msg("7: (95) exit")
__msg("(0) frame 0 insn 2 +written -16")
__msg("(0) live stack update done in 2 iterations")
__msg("7: (95) exit")
__not_msg("(0) frame 0 insn 2")
__msg("(0) live stack update done in 1 iterations")
__naked void must_write_not_same_slot(void)
{
	asm volatile (
	"call %[bpf_get_prandom_u32];"
	"r1 = -8;"
	"if r0 > 42 goto 1f;"
	"r1 = -16;"
"1:"
	"r2 = r10;"
	"r2 += r1;"
	"*(u64 *)(r2 + 0) = r0;"
	"exit;"
	:: __imm(bpf_get_prandom_u32)
	: __clobber_all);
}

SEC("socket")
__log_level(2)
__msg("(0) frame 0 insn 0 +written -8,-16")
__msg("(0) live stack update done in 2 iterations")
__msg("(0) frame 0 insn 0 +written -8")
__msg("(0) live stack update done in 2 iterations")
__naked void must_write_not_same_type(void)
{
	asm volatile (
	"*(u64*)(r10 - 8) = 0;"
	"r2 = r10;"
	"r2 += -8;"
	"r1 = %[map] ll;"
	"call %[bpf_map_lookup_elem];"
	"if r0 != 0 goto 1f;"
	"r0 = r10;"
	"r0 += -16;"
"1:"
	"*(u64 *)(r0 + 0) = 42;"
	"exit;"
	:
        : __imm(bpf_get_prandom_u32),
	  __imm(bpf_map_lookup_elem),
	  __imm_addr(map)
	: __clobber_all);
}

SEC("socket")
__log_level(2)
__msg("(2,4) frame 0 insn 4 +written -8")
__msg("(2,4) live stack update done in 2 iterations")
__msg("(0) frame 0 insn 2 +written -8")
__msg("(0) live stack update done in 2 iterations")
__naked void caller_stack_write(void)
{
	asm volatile (
	"r1 = r10;"
	"r1 += -8;"
	"call write_first_param;"
	"exit;"
	::: __clobber_all);
}

static __used __naked void write_first_param(void)
{
	asm volatile (
	"*(u64 *)(r1 + 0) = 7;"
	"r0 = 0;"
	"exit;"
	::: __clobber_all);
}

SEC("socket")
__log_level(2)
/* caller_stack_read() function */
__msg("2: .12345.... (85) call pc+4")
__msg("5: .12345.... (85) call pc+1")
__msg("6: 0......... (95) exit")
/* read_first_param() function */
__msg("7: .1........ (79) r0 = *(u64 *)(r1 +0)")
__msg("8: 0......... (95) exit")
/* update for callsite at (2) */
__msg("(2,7) frame 0 insn 7 +live -8")
__msg("(2,7) live stack update done in 2 iterations")
__msg("(0) frame 0 insn 2 +live -8")
__msg("(0) live stack update done in 2 iterations")
/* update for callsite at (5) */
__msg("(5,7) frame 0 insn 7 +live -16")
__msg("(5,7) live stack update done in 2 iterations")
__msg("(0) frame 0 insn 5 +live -16")
__msg("(0) live stack update done in 2 iterations")
__naked void caller_stack_read(void)
{
	asm volatile (
	"r1 = r10;"
	"r1 += -8;"
	"call read_first_param;"
	"r1 = r10;"
	"r1 += -16;"
	"call read_first_param;"
	"exit;"
	::: __clobber_all);
}

static __used __naked void read_first_param(void)
{
	asm volatile (
	"r0 = *(u64 *)(r1 + 0);"
	"exit;"
	::: __clobber_all);
}

SEC("socket")
__flag(BPF_F_TEST_STATE_FREQ)
__log_level(2)
/* read_first_param2() function */
__msg(" 9: .1........ (79) r0 = *(u64 *)(r1 +0)")
__msg("10: .......... (b7) r0 = 0")
__msg("11: 0......... (05) goto pc+0")
__msg("12: 0......... (95) exit")
/*
 * The purpose of the test is to check that checkpoint in
 * read_first_param2() stops path traversal. This will only happen if
 * verifier understands that fp[0]-8 at insn (12) is not alive.
 */
__msg("12: safe")
__msg("processed 20 insns")
__naked void caller_stack_pruning(void)
{
	asm volatile (
	"call %[bpf_get_prandom_u32];"
	"if r0 == 42 goto 1f;"
	"r0 = %[map] ll;"
"1:"
	"*(u64 *)(r10 - 8) = r0;"
	"r1 = r10;"
	"r1 += -8;"
	/*
	 * fp[0]-8 is either pointer to map or a scalar,
	 * preventing state pruning at checkpoint created for call.
	 */
	"call read_first_param2;"
	"exit;"
	:
	: __imm(bpf_get_prandom_u32),
	  __imm_addr(map)
	: __clobber_all);
}

static __used __naked void read_first_param2(void)
{
	asm volatile (
	"r0 = *(u64 *)(r1 + 0);"
	"r0 = 0;"
	/*
	 * Checkpoint at goto +0 should fire,
	 * as caller stack fp[0]-8 is not alive at this point.
	 */
	"goto +0;"
	"exit;"
	::: __clobber_all);
}

SEC("socket")
__flag(BPF_F_TEST_STATE_FREQ)
__failure
__msg("R1 type=scalar expected=map_ptr")
__naked void caller_stack_pruning_callback(void)
{
	asm volatile (
	"r0 = %[map] ll;"
	"*(u64 *)(r10 - 8) = r0;"
	"r1 = 2;"
	"r2 = loop_cb ll;"
	"r3 = r10;"
	"r3 += -8;"
	"r4 = 0;"
	/*
	 * fp[0]-8 is either pointer to map or a scalar,
	 * preventing state pruning at checkpoint created for call.
	 */
	"call %[bpf_loop];"
	"r0 = 42;"
	"exit;"
	:
	: __imm(bpf_get_prandom_u32),
	  __imm(bpf_loop),
	  __imm_addr(map)
	: __clobber_all);
}

static __used __naked void loop_cb(void)
{
	asm volatile (
	/*
	 * Checkpoint at function entry should not fire, as caller
	 * stack fp[0]-8 is alive at this point.
	 */
	"r6 = r2;"
	"r1 = *(u64 *)(r6 + 0);"
	"*(u64*)(r10 - 8) = 7;"
	"r2 = r10;"
	"r2 += -8;"
	"call %[bpf_map_lookup_elem];"
	/*
	 * This should stop verifier on a second loop iteration,
	 * but only if verifier correctly maintains that fp[0]-8
	 * is still alive.
	 */
	"*(u64 *)(r6 + 0) = 0;"
	"r0 = 0;"
	"exit;"
	:
	: __imm(bpf_map_lookup_elem),
	  __imm(bpf_get_prandom_u32)
	: __clobber_all);
}

/*
 * Because of a bug in verifier.c:compute_postorder()
 * the program below overflowed traversal queue in that function.
 */
SEC("socket")
__naked void syzbot_postorder_bug1(void)
{
	asm volatile (
	"r0 = 0;"
	"if r0 != 0 goto -1;"
	"exit;"
	::: __clobber_all);
}
