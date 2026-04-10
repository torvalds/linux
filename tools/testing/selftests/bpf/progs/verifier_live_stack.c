// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2025 Meta Platforms, Inc. and affiliates. */

#include <linux/bpf.h>
#include <bpf/bpf_helpers.h>
#include "bpf_misc.h"

char _license[] SEC("license") = "GPL";
struct {
	__uint(type, BPF_MAP_TYPE_HASH);
	__uint(max_entries, 1);
	__type(key, int);
	__type(value, long long);
} map SEC(".maps");

SEC("socket")
__log_level(2)
__msg("0: (79) r1 = *(u64 *)(r10 -8)        ; use: fp0-8")
__msg("1: (79) r2 = *(u64 *)(r10 -24)       ; use: fp0-24")
__msg("2: (7b) *(u64 *)(r10 -8) = r1        ; def: fp0-8")
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
__msg("2: (79) r0 = *(u64 *)(r10 -8)        ; use: fp0-8")
__msg("6: (79) r0 = *(u64 *)(r10 -16)       ; use: fp0-16")
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
__msg("stack use/def subprog#0 must_write_not_same_slot (d0,cs0):")
__msg("6: (7b) *(u64 *)(r2 +0) = r0{{$}}")
__msg("Live regs before insn:")
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
__msg("0: (7a) *(u64 *)(r10 -8) = 0         ; def: fp0-8")
__msg("5: (85) call bpf_map_lookup_elem#1   ; use: fp0-8h")
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
/* Callee writes fp[0]-8: stack_use at call site has slots 0,1 live */
__msg("stack use/def subprog#0 caller_stack_write (d0,cs0):")
__msg("2: (85) call pc+1{{$}}")
__msg("stack use/def subprog#1 write_first_param (d1,cs2):")
__msg("4: (7a) *(u64 *)(r1 +0) = 7          ; def: fp0-8")
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
__msg("stack use/def subprog#0 caller_stack_read (d0,cs0):")
__msg("2: (85) call pc+{{.*}}                   ; use: fp0-8{{$}}")
__msg("5: (85) call pc+{{.*}}                   ; use: fp0-16{{$}}")
__msg("stack use/def subprog#1 read_first_param (d1,cs2):")
__msg("7: (79) r0 = *(u64 *)(r1 +0)         ; use: fp0-8{{$}}")
__msg("8: (95) exit")
__msg("stack use/def subprog#1 read_first_param (d1,cs5):")
__msg("7: (79) r0 = *(u64 *)(r1 +0)         ; use: fp0-16{{$}}")
__msg("8: (95) exit")
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
/* fp0-8 consumed at insn 9, dead by insn 11. stack_def at insn 4 kills slots 0,1. */
__msg("4: (7b) *(u64 *)(r10 -8) = r0        ; def: fp0-8")
/* stack_use at call site: callee reads fp0-8, slots 0,1 live */
__msg("7: (85) call pc+{{.*}}               ; use: fp0-8")
/* read_first_param2: no caller stack live inside callee after first read */
__msg("9: (79) r0 = *(u64 *)(r1 +0)         ; use: fp0-8")
__msg("10: (b7) r0 = 0{{$}}")
__msg("11: (05) goto pc+0{{$}}")
__msg("12: (95) exit")
/*
 * Checkpoint at goto +0 fires because fp0-8 is dead → state pruning.
 */
__msg("12: safe")
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

struct {
        __uint(type, BPF_MAP_TYPE_PROG_ARRAY);
        __uint(max_entries, 1);
        __type(key, __u32);
        __type(value, __u32);
} map_array SEC(".maps");

SEC("socket")
__failure __msg("invalid read from stack R2 off=-1024 size=8")
__flag(BPF_F_TEST_STATE_FREQ)
__naked unsigned long caller_stack_write_tail_call(void)
{
        asm volatile (
	"r6 = r1;"
	"*(u64 *)(r10 - 8) = -8;"
        "call %[bpf_get_prandom_u32];"
        "if r0 != 42 goto 1f;"
        "goto 2f;"
  "1:"
        "*(u64 *)(r10 - 8) = -1024;"
  "2:"
        "r1 = r6;"
        "r2 = r10;"
        "r2 += -8;"
        "call write_tail_call;"
        "r1 = *(u64 *)(r10 - 8);"
        "r2 = r10;"
        "r2 += r1;"
        "r0 = *(u64 *)(r2 + 0);"
        "exit;"
        :: __imm(bpf_get_prandom_u32)
	: __clobber_all);
}

static __used __naked unsigned long write_tail_call(void)
{
        asm volatile (
        "r6 = r2;"
        "r2 = %[map_array] ll;"
        "r3 = 0;"
        "call %[bpf_tail_call];"
        "*(u64 *)(r6 + 0) = -16;"
        "r0 = 0;"
        "exit;"
	:
	: __imm(bpf_tail_call),
          __imm_addr(map_array)
        : __clobber_all);
}
