// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2025 Meta Platforms, Inc. and affiliates. */

#include <linux/bpf.h>
#include <bpf/bpf_helpers.h>
#include "../../../include/linux/filter.h"
#include "bpf_misc.h"

char _license[] SEC("license") = "GPL";
struct {
	__uint(type, BPF_MAP_TYPE_HASH);
	__uint(max_entries, 1);
	__type(key, int);
	__type(value, long long);
} map SEC(".maps");

struct {
	__uint(type, BPF_MAP_TYPE_ARRAY);
	__uint(max_entries, 1);
	__type(key, __u32);
	__type(value, __u64);
} array_map_8b SEC(".maps");

const char snprintf_u64_fmt[] = "%llu";

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
__success
__naked void arg_track_join_convergence(void)
{
	asm volatile (
	"r1 = 1;"
	"r2 = 2;"
	"call arg_track_join_convergence_subprog;"
	"r0 = 0;"
	"exit;"
	::: __clobber_all);
}

static __used __naked void arg_track_join_convergence_subprog(void)
{
	asm volatile (
	"if r1 == 0 goto 1f;"
	"r0 = r1;"
	"goto 2f;"
"1:"
	"r0 = r2;"
"2:"
	"r0 = 0;"
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

/* Test precise subprog stack access analysis.
 * Caller passes fp-32 (SPI 3) to callee that only accesses arg+0 and arg+8
 * (SPIs 3 and 2). Slots 0 and 1 should NOT be live at the call site.
 *
 * Insn layout:
 *   0: *(u64*)(r10 - 8) = 0      write SPI 0
 *   1: *(u64*)(r10 - 16) = 0     write SPI 1
 *   2: *(u64*)(r10 - 24) = 0     write SPI 2
 *   3: *(u64*)(r10 - 32) = 0     write SPI 3
 *   4: r1 = r10
 *   5: r1 += -32
 *   6: call precise_read_two      passes fp-32 (SPI 3)
 *   7: r0 = 0
 *   8: exit
 *
 * At insn 6 only SPIs 2,3 should be live (slots 4-7, 0xf0).
 * SPIs 0,1 are written but never read → dead.
 */
SEC("socket")
__log_level(2)
__msg("6: (85) call pc+{{.*}}                   ; use: fp0-24 fp0-32{{$}}")
__naked void subprog_precise_stack_access(void)
{
	asm volatile (
	"*(u64 *)(r10 - 8) = 0;"
	"*(u64 *)(r10 - 16) = 0;"
	"*(u64 *)(r10 - 24) = 0;"
	"*(u64 *)(r10 - 32) = 0;"
	"r1 = r10;"
	"r1 += -32;"
	"call precise_read_two;"
	"r0 = 0;"
	"exit;"
	::: __clobber_all);
}

/* Callee reads only at arg+0 (SPI 3) and arg+8 (SPI 2) */
static __used __naked void precise_read_two(void)
{
	asm volatile (
	"r0 = *(u64 *)(r1 + 0);"
	"r2 = *(u64 *)(r1 + 8);"
	"r0 = 0;"
	"exit;"
	::: __clobber_all);
}

/* Test that multi-level subprog calls (callee passes arg-derived ptr
 * to another BPF subprog) are analyzed precisely.
 *
 * Caller passes fp-32 (SPI 3). The callee forwards it to inner_callee.
 * inner_callee only reads at offset 0 from the pointer.
 * The analysis recurses into forward_to_inner -> inner_callee and
 * determines only SPI 3 is accessed (slots 6-7, 0xc0), not all of SPIs 0-3.
 *
 * Insn layout:
 *   0: *(u64*)(r10 - 8) = 0      write SPI 0
 *   1: *(u64*)(r10 - 16) = 0     write SPI 1
 *   2: *(u64*)(r10 - 24) = 0     write SPI 2
 *   3: *(u64*)(r10 - 32) = 0     write SPI 3
 *   4: r1 = r10
 *   5: r1 += -32
 *   6: call forward_to_inner      passes fp-32 (SPI 3)
 *   7: r0 = 0
 *   8: exit
 */
SEC("socket")
__log_level(2)
__msg("6: (85) call pc+{{.*}}                   ; use: fp0-32{{$}}")
__naked void subprog_multilevel_conservative(void)
{
	asm volatile (
	"*(u64 *)(r10 - 8) = 0;"
	"*(u64 *)(r10 - 16) = 0;"
	"*(u64 *)(r10 - 24) = 0;"
	"*(u64 *)(r10 - 32) = 0;"
	"r1 = r10;"
	"r1 += -32;"
	"call forward_to_inner;"
	"r0 = 0;"
	"exit;"
	::: __clobber_all);
}

/* Forwards arg to another subprog */
static __used __naked void forward_to_inner(void)
{
	asm volatile (
	"call inner_callee;"
	"r0 = 0;"
	"exit;"
	::: __clobber_all);
}

static __used __naked void inner_callee(void)
{
	asm volatile (
	"r0 = *(u64 *)(r1 + 0);"
	"r0 = 0;"
	"exit;"
	::: __clobber_all);
}

/* Test multi-frame precision loss: callee consumes caller stack early,
 * but static liveness keeps it live at pruning points inside callee.
 *
 * Caller stores map_ptr or scalar(42) at fp-8, then calls
 * consume_and_call_inner. The callee reads fp0-8 at entry (consuming
 * the slot), then calls do_nothing2. After do_nothing2 returns (a
 * pruning point), fp-8 should be dead -- the read already happened.
 * But because the call instruction's stack_use includes SPI 0, the
 * static live_stack_before at insn 7 is 0x1, keeping fp-8 live inside
 * the callee and preventing state pruning between the two paths.
 *
 * Insn layout:
 *   0: call bpf_get_prandom_u32
 *   1: if r0 == 42 goto pc+2    -> insn 4
 *   2: r0 = map ll (ldimm64 part1)
 *   3: (ldimm64 part2)
 *   4: *(u64)(r10 - 8) = r0     fp-8 = map_ptr OR scalar(42)
 *   5: r1 = r10
 *   6: r1 += -8
 *   7: call consume_and_call_inner
 *   8: r0 = 0
 *   9: exit
 *
 * At insn 7, live_stack_before = 0x3 (slots 0-1 live due to stack_use).
 * At insn 8, live_stack_before = 0x0 (SPI 0 dead, caller doesn't need it).
 */
SEC("socket")
__flag(BPF_F_TEST_STATE_FREQ)
__log_level(2)
__success
__msg(" 7: (85) call pc+{{.*}}                   ; use: fp0-8")
__msg(" 8: {{.*}} (b7)")
__naked void callee_consumed_caller_stack(void)
{
	asm volatile (
	"call %[bpf_get_prandom_u32];"
	"if r0 == 42 goto 1f;"
	"r0 = %[map] ll;"
"1:"
	"*(u64 *)(r10 - 8) = r0;"
	"r1 = r10;"
	"r1 += -8;"
	"call consume_and_call_inner;"
	"r0 = 0;"
	"exit;"
	:
	: __imm(bpf_get_prandom_u32),
	  __imm_addr(map)
	: __clobber_all);
}

static __used __naked void consume_and_call_inner(void)
{
	asm volatile (
	"r0 = *(u64 *)(r1 + 0);"	/* read fp[0]-8 into caller-saved r0 */
	"call do_nothing2;"		/* inner call clobbers r0 */
	"r0 = 0;"
	"goto +0;"			/* checkpoint */
	"r0 = 0;"
	"goto +0;"			/* checkpoint */
	"r0 = 0;"
	"goto +0;"			/* checkpoint */
	"r0 = 0;"
	"goto +0;"			/* checkpoint */
	"exit;"
	::: __clobber_all);
}

static __used __naked void do_nothing2(void)
{
	asm volatile (
	"r0 = 0;"
	"r0 = 0;"
	"r0 = 0;"
	"r0 = 0;"
	"r0 = 0;"
	"r0 = 0;"
	"r0 = 0;"
	"exit;"
	::: __clobber_all);
}

/*
 * Reproducer for unsound pruning when clean_verifier_state() promotes
 * live STACK_ZERO bytes to STACK_MISC.
 *
 * Program shape:
 * - Build key at fp-4:
 *   - path A keeps key byte as STACK_ZERO;
 *   - path B writes unknown byte making it STACK_MISC.
 * - Branches merge at a prune point before map_lookup.
 * - map_lookup on ARRAY map is value-sensitive to constant zero key:
 *   - path A: const key 0 => PTR_TO_MAP_VALUE (non-NULL);
 *   - path B: non-const key => PTR_TO_MAP_VALUE_OR_NULL.
 * - Dereference lookup result without null check.
 *
 * Note this behavior won't trigger at fp-8, since the verifier will
 * track 32-bit scalar spill differently as spilled_ptr.
 *
 * Correct verifier behavior: reject (path B unsafe).
 * With blanket STACK_ZERO->STACK_MISC promotion on live slots, cached path A
 * state can be generalized and incorrectly prune path B, making program load.
 */
SEC("socket")
__flag(BPF_F_TEST_STATE_FREQ)
__failure __msg("R0 invalid mem access 'map_value_or_null'")
__naked void stack_zero_to_misc_unsound_array_lookup(void)
{
	asm volatile (
	/* key at fp-4: all bytes STACK_ZERO */
	"*(u32 *)(r10 - 4) = 0;"
	"call %[bpf_get_prandom_u32];"
	/* fall-through (path A) explored first */
	"if r0 != 0 goto l_nonconst%=;"
	/* path A: keep key constant zero */
	"goto l_lookup%=;"
"l_nonconst%=:"
	/* path B: key byte turns to STACK_MISC, key no longer const */
	"*(u8 *)(r10 - 4) = r0;"
"l_lookup%=:"
	/* value-sensitive lookup */
	"r2 = r10;"
	"r2 += -4;"
	"r1 = %[array_map_8b] ll;"
	"call %[bpf_map_lookup_elem];"
	/* unsafe when lookup result is map_value_or_null */
	"r0 = *(u64 *)(r0 + 0);"
	"exit;"
	:
	: __imm(bpf_get_prandom_u32),
	  __imm(bpf_map_lookup_elem),
	  __imm_addr(array_map_8b)
	: __clobber_all);
}

/*
 * Subprog variant of stack_zero_to_misc_unsound_array_lookup.
 *
 * Check unsound pruning when a callee modifies the caller's
 * stack through a pointer argument.
 *
 * Program shape:
 *   main:
 *     *(u32)(fp - 4) = 0            key = 0 (all bytes STACK_ZERO)
 *     r1 = fp - 4
 *     call maybe_clobber_key        may overwrite key[0] with scalar
 *     <-- prune point: two states meet here -->
 *     r2 = fp - 4
 *     r1 = array_map_8b
 *     call bpf_map_lookup_elem      value-sensitive on const-zero key
 *     r0 = *(u64)(r0 + 0)           deref without null check
 *     exit
 *
 *   maybe_clobber_key(r1):
 *     r6 = r1                       save &key
 *     call bpf_get_prandom_u32
 *     if r0 == 0 goto skip          path A: key stays STACK_ZERO
 *     *(u8)(r6 + 0) = r0            path B: key[0] becomes STACK_MISC
 *   skip:
 *     r0 = 0
 *     exit
 *
 * Path A: const-zero key => array lookup => PTR_TO_MAP_VALUE => deref OK.
 * Path B: non-const key  => array lookup => PTR_TO_MAP_VALUE_OR_NULL => UNSAFE.
 *
 * If the cleaner collapses STACK_ZERO -> STACK_MISC for the live key
 * slot, path A's cached state matches path B, pruning the unsafe path.
 *
 * Correct verifier behaviour: reject.
 */
SEC("socket")
__flag(BPF_F_TEST_STATE_FREQ)
__failure __msg("R0 invalid mem access 'map_value_or_null'")
__naked void subprog_stack_zero_to_misc_unsound(void)
{
	asm volatile (
	/* key at fp-4: all bytes STACK_ZERO */
	"*(u32 *)(r10 - 4) = 0;"
	/* subprog may clobber key[0] with a scalar byte */
	"r1 = r10;"
	"r1 += -4;"
	"call maybe_clobber_key;"
	/* value-sensitive array lookup */
	"r2 = r10;"
	"r2 += -4;"
	"r1 = %[array_map_8b] ll;"
	"call %[bpf_map_lookup_elem];"
	/* unsafe when result is map_value_or_null (path B) */
	"r0 = *(u64 *)(r0 + 0);"
	"exit;"
	:
	: __imm(bpf_map_lookup_elem),
	  __imm_addr(array_map_8b)
	: __clobber_all);
}

static __used __naked void maybe_clobber_key(void)
{
	asm volatile (
	"r6 = r1;"
	"call %[bpf_get_prandom_u32];"
	/* path A (r0==0): key stays STACK_ZERO, explored first */
	"if r0 == 0 goto 1f;"
	/* path B (r0!=0): overwrite key[0] with scalar */
	"*(u8 *)(r6 + 0) = r0;"
	"1:"
	"r0 = 0;"
	"exit;"
	:: __imm(bpf_get_prandom_u32)
	: __clobber_all);
}

/*
 * Demonstrate that subprog arg spill/reload breaks arg tracking,
 * inflating caller stack liveness and preventing state pruning.
 *
 * modifier2(fp-24) has two paths: one writes a scalar to *(r1+8)
 * = caller fp-16, the other leaves it as zero.  After modifier2
 * returns, fp-16 is never read again — it is dead.
 *
 * spill_reload_reader2(fp-24) only reads caller fp-8 via
 * *(r1+16), but it spills r1 across a helper call.  This
 * breaks compute_subprog_arg_access(): the reload from callee
 * stack cannot be connected back to arg1, so arg1 access goes
 * "all (conservative)".  At the call site (r1 = fp-24, slot 5)
 * apply_callee_stack_access() marks slots 0..5 as stack_use —
 * pulling fp-16 (slots 2-3) into live_stack_before even though
 * the reader never touches it.
 *
 * Result: at modifier2's return point two states with different
 * fp-16 values cannot be pruned.
 *
 * With correct (or old dynamic) liveness fp-16 is dead at that
 * point and the states prune → "6: safe" appears in the log.
 */
SEC("socket")
__flag(BPF_F_TEST_STATE_FREQ)
__log_level(2)
__success
__msg("6: safe")
__naked void spill_reload_inflates_stack_liveness(void)
{
	asm volatile (
	/* struct at fp-24: { ctx; ptr; tail; } */
	"*(u64 *)(r10 - 24) = r1;"		/* fp-24 = ctx */
	"*(u64 *)(r10 - 16) = r1;"		/* fp-16 = ctx (STACK_SPILL ptr) */
	"*(u64 *)(r10 - 8) = 0;"		/* fp-8  = tail */
	/* modifier2 writes different values to fp-16 on two paths */
	"r1 = r10;"
	"r1 += -24;"
	"call modifier2;"
	/* insn 6: prune point — two states with different fp-16
	 * path A: fp-16 = STACK_MISC  (scalar overwrote pointer)
	 * path B: fp-16 = STACK_SPILL (original ctx pointer)
	 * STACK_MISC does NOT subsume STACK_SPILL(ptr),
	 * so pruning fails unless fp-16 is cleaned (dead).
	 */
	"r1 = r10;"
	"r1 += -24;"
	"call spill_reload_reader2;"		/* reads fp-8 via *(r1+16) */
	"r0 = 0;"
	"exit;"
	::: __clobber_all);
}

/* Two paths: one writes a scalar to *(r1+8) = caller fp-16,
 * the other leaves it unchanged.  Both return 0 via separate
 * exits to prevent pruning inside the subprog at the merge.
 */
static __used __naked void modifier2(void)
{
	asm volatile (
	"r6 = r1;"
	"call %[bpf_get_prandom_u32];"
	"if r0 == 0 goto 1f;"
	"*(u64 *)(r6 + 8) = r0;"		/* fp-16 = random */
	"r0 = 0;"
	"exit;"					/* path A exit */
	"1:"
	"r0 = 0;"
	"exit;"					/* path B exit */
	:: __imm(bpf_get_prandom_u32)
	: __clobber_all);
}

/* Receives r1 = caller fp-24.  Only reads *(r1+16) = fp-8.
 * Spills r1 across a helper call → arg tracking goes conservative →
 * slots 0..5 all appear used instead of just slot 1 (fp-8).
 */
static __used __naked void spill_reload_reader2(void)
{
	asm volatile (
	"*(u64 *)(r10 - 8) = r1;"		/* spill arg1 */
	"call %[bpf_get_prandom_u32];"		/* clobbers r1-r5 */
	"r1 = *(u64 *)(r10 - 8);"		/* reload arg1 */
	"r0 = *(u64 *)(r1 + 16);"		/* read caller fp-8 */
	"r0 = 0;"
	"exit;"
	:: __imm(bpf_get_prandom_u32)
	: __clobber_all);
}

/* BTF FUNC records are not generated for kfuncs referenced
 * from inline assembly. These records are necessary for
 * libbpf to link the program. The function below is a hack
 * to ensure that BTF FUNC records are generated.
 */
void __kfunc_btf_root(void)
{
	bpf_iter_num_new(0, 0, 0);
	bpf_iter_num_next(0);
	bpf_iter_num_destroy(0);
}

/* Test that open-coded iterator kfunc arguments get precise stack
 * liveness tracking. struct bpf_iter_num is 8 bytes (1 SPI).
 *
 * Insn layout:
 *   0: *(u64*)(r10 - 8) = 0      write SPI 0 (dead)
 *   1: *(u64*)(r10 - 16) = 0     write SPI 1 (dead)
 *   2: r1 = r10
 *   3: r1 += -24                 iter state at fp-24 (SPI 2)
 *   4: r2 = 0
 *   5: r3 = 10
 *   6: call bpf_iter_num_new     defines SPI 2 (KF_ITER_NEW) → 0x0
 *   7-8: r1 = fp-24
 *   9: call bpf_iter_num_next    uses SPI 2 → 0x30
 *  10: if r0 == 0 goto 2f
 *  11: goto 1b
 *  12-13: r1 = fp-24
 *  14: call bpf_iter_num_destroy uses SPI 2 → 0x30
 *  15: r0 = 0
 *  16: exit
 *
 * At insn 6, SPI 2 is defined (KF_ITER_NEW initializes, doesn't read),
 * so it kills liveness from successors. live_stack_before = 0x0.
 * At insns 9 and 14, SPI 2 is used (iter_next/destroy read the state),
 * so live_stack_before = 0x30.
 */
SEC("socket")
__success __log_level(2)
__msg(" 6: (85) call bpf_iter_num_new{{.*}}          ; def: fp0-24{{$}}")
__msg(" 9: (85) call bpf_iter_num_next{{.*}}         ; use: fp0-24{{$}}")
__msg("14: (85) call bpf_iter_num_destroy{{.*}}      ; use: fp0-24{{$}}")
__naked void kfunc_iter_stack_liveness(void)
{
	asm volatile (
	"*(u64 *)(r10 - 8) = 0;"	/* SPI 0 - dead */
	"*(u64 *)(r10 - 16) = 0;"	/* SPI 1 - dead */
	"r1 = r10;"
	"r1 += -24;"
	"r2 = 0;"
	"r3 = 10;"
	"call %[bpf_iter_num_new];"
"1:"
	"r1 = r10;"
	"r1 += -24;"
	"call %[bpf_iter_num_next];"
	"if r0 == 0 goto 2f;"
	"goto 1b;"
"2:"
	"r1 = r10;"
	"r1 += -24;"
	"call %[bpf_iter_num_destroy];"
	"r0 = 0;"
	"exit;"
	:: __imm(bpf_iter_num_new),
	   __imm(bpf_iter_num_next),
	   __imm(bpf_iter_num_destroy)
	: __clobber_all);
}

/*
 * Test for soundness bug in static stack liveness analysis.
 *
 * The static pre-pass tracks FP-derived register offsets to determine
 * which stack slots are accessed. When a PTR_TO_STACK is spilled to
 * the stack and later reloaded, the reload (BPF_LDX) kills FP-derived
 * tracking, making subsequent accesses through the reloaded pointer
 * invisible to the static analysis.
 *
 * This causes the analysis to incorrectly mark SPI 0 as dead at the
 * merge point. clean_verifier_state() zeros it in the cached state,
 * and stacksafe() accepts the new state against STACK_INVALID,
 * enabling incorrect pruning.
 *
 * Path A (verified first): stores PTR_TO_MAP_VALUE in SPI 0
 * Path B (verified second): stores scalar 42 in SPI 0
 * After merge: reads SPI 0 through spilled/reloaded PTR_TO_STACK
 * and dereferences the result as a pointer.
 *
 * Correct behavior: reject (path B dereferences a scalar)
 * Bug behavior: accept (path B is incorrectly pruned)
 */
SEC("socket")
__flag(BPF_F_TEST_STATE_FREQ)
__failure __msg("R0 invalid mem access 'scalar'")
__naked void spill_ptr_liveness_type_confusion(void)
{
	asm volatile (
	/* Map lookup to get PTR_TO_MAP_VALUE */
	"r1 = %[map] ll;"
	"*(u32 *)(r10 - 32) = 0;"
	"r2 = r10;"
	"r2 += -32;"
	"call %[bpf_map_lookup_elem];"
	"if r0 == 0 goto l_exit%=;"
	/* r6 = PTR_TO_MAP_VALUE (callee-saved) */
	"r6 = r0;"
	/* Branch: fall-through (path A) verified first */
	"call %[bpf_get_prandom_u32];"
	"if r0 != 0 goto l_scalar%=;"
	/* Path A: store map value ptr at SPI 0 */
	"*(u64 *)(r10 - 8) = r6;"
	"goto l_merge%=;"
"l_scalar%=:"
	/* Path B: store scalar at SPI 0 */
	"r1 = 42;"
	"*(u64 *)(r10 - 8) = r1;"
"l_merge%=:"
	/*
	 * Spill PTR_TO_STACK{off=-8} to SPI 1, then reload.
	 * Reload kills FP-derived tracking, hiding the
	 * subsequent SPI 0 access from the static analysis.
	 */
	"r1 = r10;"
	"r1 += -8;"
	"*(u64 *)(r10 - 16) = r1;"
	"goto +0;"			/* checkpoint */
	"goto +0;"			/* checkpoint */
	"goto +0;"			/* checkpoint */
	"r1 = *(u64 *)(r10 - 16);"
	/* Read SPI 0 through reloaded pointer */
	"r0 = *(u64 *)(r1 + 0);"
	/* Dereference: safe for map value (path A),
	 * unsafe for scalar (path B).
	 */
	"r0 = *(u64 *)(r0 + 0);"
	"exit;"
"l_exit%=:"
	"r0 = 0;"
	"exit;"
	:
	: __imm(bpf_map_lookup_elem),
	  __imm(bpf_get_prandom_u32),
	  __imm_addr(map)
	: __clobber_all);
}

/* === Tests for 4-byte stack slot liveness granularity === */

/* Test that a 4-byte aligned write is stack_def and kills liveness.
 *
 *   0: *(u64 *)(r10 - 8) = 0      def slots 0,1 (full SPI 0)
 *   1: *(u32 *)(r10 - 8) = 0      def slot 1 (4-byte write kills slot 1)
 *   2: r0 = *(u64 *)(r10 - 8)     use slots 0,1
 *   3: r0 = 0
 *   4: exit
 *
 * At insn 1, the 4-byte write defines slot 1. Slot 0 still flows
 * backward from insn 2's read: live_stack_before = 0x1.
 */
SEC("socket")
__log_level(2)
__msg("1: (62) *(u32 *)(r10 -8) = 0         ; def: fp0-8h")
__naked void four_byte_write_kills_slot(void)
{
	asm volatile (
	"*(u64 *)(r10 - 8) = 0;"
	"*(u32 *)(r10 - 8) = 0;"
	"r0 = *(u64 *)(r10 - 8);"
	"r0 = 0;"
	"exit;"
	::: __clobber_all);
}

/* Test that a write to the upper half of an SPI is dead when only
 * the lower half is read. This was impossible at SPI granularity
 * where any read of the SPI kept the entire SPI live.
 *
 *   0: *(u32 *)(r10 - 8) = 0      def slot 1 (DEAD: never read)
 *   1: *(u32 *)(r10 - 4) = 0      def slot 0
 *   2: r0 = *(u32 *)(r10 - 4)     use slot 0 only
 *   3: r0 = 0
 *   4: exit
 *
 * At insn 0, nothing is live (0x0). Previously at SPI granularity,
 * the read at insn 2 would mark the full SPI 0 as live and the
 * 4-byte writes wouldn't count as def, so insn 0 would have had
 * SPI 0 live (0x1).
 */
SEC("socket")
__log_level(2)
__msg("0: (62) *(u32 *)(r10 -8) = 0         ; def: fp0-8h")
__msg("2: (61) r0 = *(u32 *)(r10 -4)        ; use: fp0-4h")
__naked void dead_half_spi_write(void)
{
	asm volatile (
	"*(u32 *)(r10 - 8) = 0;"
	"*(u32 *)(r10 - 4) = 0;"
	"r0 = *(u32 *)(r10 - 4);"
	"r0 = 0;"
	"exit;"
	::: __clobber_all);
}

/* Test that a 4-byte read from the upper half of SPI 0 makes only
 * slot 1 live (0x2), not the full SPI (0x3).
 *
 *   0: *(u64 *)(r10 - 8) = 0      def slots 0,1
 *   1: r0 = *(u32 *)(r10 - 8)     use slot 1 only (upper half)
 *   2: r0 = 0
 *   3: exit
 *
 * At insn 1, live_stack_before = 0x2 (slot 1 only).
 */
SEC("socket")
__log_level(2)
__msg("1: (61) r0 = *(u32 *)(r10 -8)        ; use: fp0-8h")
__naked void four_byte_read_upper_half(void)
{
	asm volatile (
	"*(u64 *)(r10 - 8) = 0;"
	"r0 = *(u32 *)(r10 - 8);"
	"r0 = 0;"
	"exit;"
	::: __clobber_all);
}

/* Test that a 2-byte write does NOT count as stack_def.
 * Sub-4-byte writes don't fully cover a 4-byte slot,
 * so liveness passes through.
 *
 *   0: *(u64 *)(r10 - 8) = 0      def slots 0,1
 *   1: *(u16 *)(r10 - 4) = 0      NOT stack_def (2 < 4 bytes)
 *   2: r0 = *(u32 *)(r10 - 4)     use slot 0
 *   3: r0 = 0
 *   4: exit
 *
 * At insn 1, slot 0 still live (0x1) because 2-byte write
 * didn't kill it.
 */
SEC("socket")
__log_level(2)
__msg("0: (7a) *(u64 *)(r10 -8) = 0         ; def: fp0-8")
__msg("1: (6a) *(u16 *)(r10 -4) = 0{{$}}")
__msg("2: (61) r0 = *(u32 *)(r10 -4)        ; use: fp0-4h")
__naked void two_byte_write_no_kill(void)
{
	asm volatile (
	"*(u64 *)(r10 - 8) = 0;"
	"*(u16 *)(r10 - 4) = 0;"
	"r0 = *(u32 *)(r10 - 4);"
	"r0 = 0;"
	"exit;"
	::: __clobber_all);
}

/* Test that a 1-byte write does NOT count as stack_def.
 *
 *   0: *(u64 *)(r10 - 8) = 0      def slots 0,1
 *   1: *(u8 *)(r10 - 4) = 0       NOT stack_def (1 < 4 bytes)
 *   2: r0 = *(u32 *)(r10 - 4)     use slot 0
 *   3: r0 = 0
 *   4: exit
 *
 * At insn 1, slot 0 still live (0x1).
 */
SEC("socket")
__log_level(2)
__msg("0: (7a) *(u64 *)(r10 -8) = 0         ; def: fp0-8")
__msg("1: (72) *(u8 *)(r10 -4) = 0")
__msg("2: (61) r0 = *(u32 *)(r10 -4)        ; use: fp0-4h")
__naked void one_byte_write_no_kill(void)
{
	asm volatile (
	"*(u64 *)(r10 - 8) = 0;"
	"*(u8 *)(r10 - 4) = 0;"
	"r0 = *(u32 *)(r10 - 4);"
	"r0 = 0;"
	"exit;"
	::: __clobber_all);
}

/* Test stack access beyond fp-256 exercising the second bitmask word.
 * fp-264 is SPI 32, slots 64-65, which are bits 0-1 of live_stack[1].
 *
 *   0: *(u64 *)(r10 - 264) = 0     def slots 64,65
 *   1: r0 = *(u64 *)(r10 - 264)    use slots 64,65
 *   2: r0 = 0
 *   3: exit
 *
 * At insn 1, live_stack high word has bits 0,1 set: 0x3:0x0.
 */
SEC("socket")
__log_level(2)
__msg("1: (79) r0 = *(u64 *)(r10 -264)      ; use: fp0-264")
__naked void high_stack_second_bitmask_word(void)
{
	asm volatile (
	"*(u64 *)(r10 - 264) = 0;"
	"r0 = *(u64 *)(r10 - 264);"
	"r0 = 0;"
	"exit;"
	::: __clobber_all);
}

/* Test that two separate 4-byte writes to each half of an SPI
 * together kill liveness for the full SPI.
 *
 *   0: *(u32 *)(r10 - 8) = 0      def slot 1 (upper half)
 *   1: *(u32 *)(r10 - 4) = 0      def slot 0 (lower half)
 *   2: r0 = *(u64 *)(r10 - 8)     use slots 0,1
 *   3: r0 = 0
 *   4: exit
 *
 * At insn 0: live_stack_before = 0x0 (both slots killed by insns 0,1).
 * At insn 1: live_stack_before = 0x2 (slot 1 still live, slot 0 killed here).
 */
SEC("socket")
__log_level(2)
__msg("0: (62) *(u32 *)(r10 -8) = 0         ; def: fp0-8h")
__msg("1: (62) *(u32 *)(r10 -4) = 0         ; def: fp0-4h")
__naked void two_four_byte_writes_kill_full_spi(void)
{
	asm volatile (
	"*(u32 *)(r10 - 8) = 0;"
	"*(u32 *)(r10 - 4) = 0;"
	"r0 = *(u64 *)(r10 - 8);"
	"r0 = 0;"
	"exit;"
	::: __clobber_all);
}

/* Test that 4-byte writes on both branches kill a slot at the
 * join point. Previously at SPI granularity, a 4-byte write was
 * not stack_def, so liveness would flow backward through the
 * branch that only had a 4-byte write.
 *
 *   0: call bpf_get_prandom_u32
 *   1: if r0 != 0 goto 1f
 *   2: *(u64 *)(r10 - 8) = 0       path A: def slots 0,1
 *   3: goto 2f
 * 1:4: *(u32 *)(r10 - 4) = 0       path B: def slot 0
 * 2:5: r0 = *(u32 *)(r10 - 4)      use slot 0
 *   6: r0 = 0
 *   7: exit
 *
 * Both paths define slot 0 before the read. At insn 1 (branch),
 * live_stack_before = 0x0 because slot 0 is killed on both paths.
 */
SEC("socket")
__log_level(2)
__msg("1: (55) if r0 != 0x0 goto pc+2")
__msg("2: (7a) *(u64 *)(r10 -8) = 0         ; def: fp0-8")
__msg("3: (05) goto pc+1")
__msg("4: (62) *(u32 *)(r10 -4) = 0         ; def: fp0-4h")
__msg("5: (61) r0 = *(u32 *)(r10 -4)        ; use: fp0-4h")
__naked void both_branches_kill_slot(void)
{
	asm volatile (
	"call %[bpf_get_prandom_u32];"
	"if r0 != 0 goto 1f;"
	"*(u64 *)(r10 - 8) = 0;"
	"goto 2f;"
"1:"
	"*(u32 *)(r10 - 4) = 0;"
"2:"
	"r0 = *(u32 *)(r10 - 4);"
	"r0 = 0;"
	"exit;"
	:: __imm(bpf_get_prandom_u32)
	: __clobber_all);
}

/* Soundness: cleaning the dead upper half of an SPI must not
 * affect the live lower half's type information for pruning.
 *
 * Both halves of SPI 0 are written separately. Only the lower
 * half (slot 0) is used as a 4-byte map key. The upper half
 * (slot 1) is dead and cleaned to STACK_INVALID.
 *
 * Path A: key stays 0 (STACK_ZERO) → non-null array lookup
 * Path B: key byte turns STACK_MISC → may-null array lookup
 * Deref without null check: safe for A, unsafe for B.
 *
 * If half-SPI cleaning incorrectly corrupted the live half's
 * type info, path A's cached state could generalize and unsoundly
 * prune path B.
 *
 * Expected: reject (path B unsafe).
 */
SEC("socket")
__flag(BPF_F_TEST_STATE_FREQ)
__failure __msg("R0 invalid mem access 'map_value_or_null'")
__naked void half_spi_clean_preserves_stack_zero(void)
{
	asm volatile (
	"*(u32 *)(r10 - 4) = 0;"           /* slot 0: STACK_ZERO */
	"*(u32 *)(r10 - 8) = 0;"           /* slot 1: STACK_ZERO (dead) */
	"call %[bpf_get_prandom_u32];"
	"if r0 != 0 goto l_nonconst%=;"
	"goto l_lookup%=;"
"l_nonconst%=:"
	"*(u8 *)(r10 - 4) = r0;"           /* slot 0: STACK_MISC */
"l_lookup%=:"
	"r2 = r10;"
	"r2 += -4;"
	"r1 = %[array_map_8b] ll;"
	"call %[bpf_map_lookup_elem];"
	"r0 = *(u64 *)(r0 + 0);"           /* unsafe if null */
	"exit;"
	:
	: __imm(bpf_get_prandom_u32),
	  __imm(bpf_map_lookup_elem),
	  __imm_addr(array_map_8b)
	: __clobber_all);
}

/*
 * Model of scx_lavd's pick_idle_cpu_at_cpdom iat block:
 * conditional block with helper call and temporary stack spill,
 * spill dead after merge.
 *
 * Path A (fall-through): spill r6 to fp-8 across helper call
 * Path B (branch taken): skip the block entirely
 * At merge (insn 6): fp-8 is dead (never read after merge)
 *
 * Static liveness marks fp-8 dead at merge. clean_verifier_state()
 * converts path A's STACK_SPILL to STACK_INVALID. Path B has
 * STACK_INVALID. stacksafe() matches -> path B pruned -> "6: safe".
 */
SEC("socket")
__flag(BPF_F_TEST_STATE_FREQ)
__success
__log_level(2)
__msg("6: safe")
__naked void dead_spill_at_merge_enables_pruning(void)
{
	asm volatile (
	"call %[bpf_get_prandom_u32];"
	"r6 = 7;"
	"if r0 != 0 goto l_skip%=;"
	/* conditional block: spill, call, reload */
	"*(u64 *)(r10 - 8) = r6;"
	"call %[bpf_get_prandom_u32];"
	"r6 = *(u64 *)(r10 - 8);"
"l_skip%=:"
	/* fp-8 dead. Path B pruned here -> "6: safe" */
	"r0 = r6;"
	"exit;"
	:
	: __imm(bpf_get_prandom_u32)
	: __clobber_all);
}

/*
 * FP-offset tracking loses precision on second ADD, killing all liveness.
 *
 * fp_off_insn_xfer() handles "FP itself + negative imm" precisely
 * (e.g. r6 = r10; r6 += -24 -> slot 5).  But any subsequent ADD/SUB
 * on a register that already has non-zero spis falls through to
 * spis_set_all(), because the code only handles the FP-itself case.
 *
 * A write through this imprecise register enters the non-zero-spis
 * branch of set_indirect_stack_access(), which OR's the all-ones
 * mask into stack_def.  The backward liveness equation
 *
 *   stack_in = (stack_out & ~stack_def) | stack_use
 *
 * sees ~ALL = 0, killing ALL slot liveness at that instruction.
 *
 * At the merge pruning point, live_stack_before is empty.
 * clean_verifier_state() marks fp-8 as STACK_INVALID.
 * stacksafe() skips STACK_INVALID (line "continue"), so pruning
 * succeeds regardless of the current state's fp-8 value.
 * Path B is pruned, its null deref is never explored.
 *
 * Correct behavior: reject (path B dereferences NULL).
 * Bug behavior: accept (path B pruned away).
 */
SEC("socket")
__flag(BPF_F_TEST_STATE_FREQ)
__failure __msg("R1 invalid mem access 'scalar'")
__naked void fp_add_loses_precision_kills_liveness(void)
{
	asm volatile (
	"call %[bpf_get_prandom_u32];"
	"if r0 != 0 goto l_pathB%=;"

	/* Path A (fall-through, explored first): fp-8 = 0 */
	"r1 = 0;"
	"*(u64 *)(r10 - 8) = r1;"
	"goto l_merge%=;"

"l_pathB%=:"
	/* Path B (explored second): fp-8 = 42 */
	"r1 = 42;"
	"*(u64 *)(r10 - 8) = r1;"

"l_merge%=:"
	/*
	 * Create imprecise FP-derived register.
	 * r6 = r10 - 24 gets precise slot 5.
	 * r6 += 8 hits the else branch (spis non-zero, delta > 0)
	 * and sets spis to ALL.  r6 is actually r10-16.
	 */
	"r6 = r10;"
	"r6 += -24;"
	"r6 += 8;"

	/*
	 * Write through imprecise r6.  Actually writes to fp-16
	 * (does NOT touch fp-8), but liveness marks ALL slots
	 * as stack_def, killing fp-8's liveness.
	 */
	"r7 = 0;"
	"*(u64 *)(r6 + 0) = r7;"

	/* Read fp-8: liveness says dead, but value is needed. */
	"r2 = *(u64 *)(r10 - 8);"
	"if r2 == 42 goto l_danger%=;"

	/* r2 != 42 (path A: r2 == 0): safe exit */
	"r0 = 0;"
	"exit;"

"l_danger%=:"
	/* Only reachable from path B (r2 == 42): null deref */
	"r1 = 0;"
	"r0 = *(u64 *)(r1 + 0);"
	"exit;"
	:
	: __imm(bpf_get_prandom_u32)
	: __clobber_all);
}

SEC("socket")
__flag(BPF_F_TEST_STATE_FREQ)
__failure __msg("R1 invalid mem access 'scalar'")
__naked void fp_spill_loses_precision_kills_liveness(void)
{
	asm volatile (
	"call %[bpf_get_prandom_u32];"
	"if r0 != 0 goto l_pathB%=;"

	"r1 = 0;"
	"*(u64 *)(r10 - 8) = r1;"
	"goto l_merge%=;"

"l_pathB%=:"
	"r1 = 42;"
	"*(u64 *)(r10 - 8) = r1;"

"l_merge%=:"
	"r6 = r10;"
	"r6 += -64;"
	"*(u64 *)(r10 - 160) = r6;"
	"r6 = *(u64 *)(r10 - 160);"

	"r7 = 0;"
	"*(u64 *)(r6 + 0) = r7;"

	"r2 = *(u64 *)(r10 - 8);"
	"if r2 == 42 goto l_danger%=;"

	"r0 = *(u64 *)(r10 - 56);"
	"exit;"

"l_danger%=:"
	"r1 = 0;"
	"r0 = *(u64 *)(r1 + 0);"
	"exit;"
	:
	: __imm(bpf_get_prandom_u32)
	: __clobber_all);
}

/* === Tests for frame-based AT_FP tracking === */

/*
 * Test 1: conditional_stx_in_subprog
 * Subprog conditionally writes caller's slot.
 * Verify slot stays live (backward pass handles conditional def via CFG).
 *
 * Main writes fp-8=42, calls cond_writer(fp-8), reads fp-8.
 * cond_writer only writes on one path → parent_def only on that path.
 * The backward parent_live correctly keeps fp-8 live at entry
 * (conditional write doesn't kill liveness at the join).
 */
SEC("socket")
__log_level(2)
/* fp-8 live at call (callee conditionally writes → slot not killed) */
__msg("1: (7b) *(u64 *)(r10 -8) = r1        ; def: fp0-8")
__msg("4: (85) call pc+2{{$}}")
__msg("5: (79) r0 = *(u64 *)(r10 -8)        ; use: fp0-8")
__naked void conditional_stx_in_subprog(void)
{
	asm volatile (
	"r1 = 42;"
	"*(u64 *)(r10 - 8) = r1;"
	"r1 = r10;"
	"r1 += -8;"
	"call cond_writer;"
	"r0 = *(u64 *)(r10 - 8);"
	"exit;"
	::: __clobber_all);
}

/* Conditionally writes to *(r1+0) */
static __used __naked void cond_writer(void)
{
	asm volatile (
	"r6 = r1;"
	"call %[bpf_get_prandom_u32];"
	"if r0 == 0 goto 1f;"
	"*(u64 *)(r6 + 0) = r0;"
	"1:"
	"r0 = 0;"
	"exit;"
	:: __imm(bpf_get_prandom_u32)
	: __clobber_all);
}

SEC("socket")
__log_level(2)
__msg("4: (85) call pc+{{.*}}                   ; use: fp0-16")
__msg("7: (85) call pc+{{.*}}                   ; use: fp0-32")
__naked void multiple_callsites_different_offsets(void)
{
	asm volatile (
	"*(u64 *)(r10 - 16) = 0;"
	"*(u64 *)(r10 - 32) = 0;"
	"r1 = r10;"
	"r1 += -16;"
	"call read_first_param;"
	"r1 = r10;"
	"r1 += -32;"
	"call read_first_param;"
	"r0 = 0;"
	"exit;"
	::: __clobber_all);
}

/*
 * Test 3: nested_fp_passthrough
 * main→A→B, main's FP forwarded to B. B accesses main's stack.
 * Verify liveness propagates through.
 *
 * Main passes fp-32 to outer_forwarder, which passes it to inner_reader.
 * inner_reader reads at arg+0 (= main's fp-32).
 * parent_live propagates transitively: inner→outer→main.
 */
SEC("socket")
__log_level(2)
/* At call to outer_forwarder: main's fp-32 (slots 6,7) should be live */
__msg("6: (85) call pc+{{.*}}                   ; use: fp0-32")
__naked void nested_fp_passthrough(void)
{
	asm volatile (
	"*(u64 *)(r10 - 8) = 0;"
	"*(u64 *)(r10 - 16) = 0;"
	"*(u64 *)(r10 - 24) = 0;"
	"*(u64 *)(r10 - 32) = 0;"
	"r1 = r10;"
	"r1 += -32;"
	"call outer_forwarder;"
	"r0 = 0;"
	"exit;"
	::: __clobber_all);
}

/* Forwards arg to inner_reader */
static __used __naked void outer_forwarder(void)
{
	asm volatile (
	"call inner_reader;"
	"r0 = 0;"
	"exit;"
	::: __clobber_all);
}

static __used __naked void inner_reader(void)
{
	asm volatile (
	"r0 = *(u64 *)(r1 + 0);"
	"r0 = 0;"
	"exit;"
	::: __clobber_all);
}

/*
 * Test 4: callee_must_write_before_read
 * Callee unconditionally writes parent slot before reading.
 * Verify slot is NOT live at call site (parent_def kills it).
 */
SEC("socket")
__log_level(2)
/* fp-8 NOT live at call: callee writes before reading (parent_def kills it) */
__msg("2: .12345.... (85) call pc+")
__naked void callee_must_write_before_read(void)
{
	asm volatile (
	"r1 = r10;"
	"r1 += -8;"
	"call write_then_read;"
	"r0 = 0;"
	"exit;"
	::: __clobber_all);
}

/* Unconditionally writes *(r1+0), then reads it back */
static __used __naked void write_then_read(void)
{
	asm volatile (
	"r6 = r1;"
	"r7 = 99;"
	"*(u64 *)(r6 + 0) = r7;"
	"r0 = *(u64 *)(r6 + 0);"
	"r0 = 0;"
	"exit;"
	::: __clobber_all);
}

/*
 * Test 5: return_site_liveness_bleeding
 * Main calls subprog twice. Slot used after one call but not the other.
 * Context-insensitive: slot conservatively live at both.
 *
 * After first call: read fp-8.
 * After second call: don't read fp-8.
 * Since parent_live is per-subprog (not per call-site),
 * fp-8 is live at both call sites.
 */
SEC("socket")
__log_level(2)
/* Both calls have fp-8 live due to context-insensitive parent_live */
__msg("3: (85) call pc+{{.*}}                   ; use: fp0-8")
__msg("7: (85) call pc+{{.*}}                   ; use: fp0-8")
__naked void return_site_liveness_bleeding(void)
{
	asm volatile (
	"*(u64 *)(r10 - 8) = 0;"
	"r1 = r10;"
	"r1 += -8;"
	"call read_first_param;"
	"r0 = *(u64 *)(r10 - 8);"
	"r1 = r10;"
	"r1 += -8;"
	"call read_first_param;"
	"r0 = 0;"
	"exit;"
	::: __clobber_all);
}

SEC("socket")
__log_level(2)
__msg("9: (85) call bpf_loop#181            ; use: fp0-16")
__naked void callback_conditional_read_beyond_ctx(void)
{
	asm volatile (
	"r1 = 42;"
	"*(u64 *)(r10 - 8) = r1;"
	"*(u64 *)(r10 - 16) = r1;"
	"r1 = 2;"
	"r2 = cb_cond_read ll;"
	"r3 = r10;"
	"r3 += -8;"
	"r4 = 0;"
	"call %[bpf_loop];"
	"r0 = 0;"
	"exit;"
	:: __imm(bpf_loop)
	: __clobber_all);
}

/* Callback conditionally reads *(ctx - 8) = caller fp-16 */
static __used __naked void cb_cond_read(void)
{
	asm volatile (
	"r6 = r2;"
	"call %[bpf_get_prandom_u32];"
	"if r0 == 0 goto 1f;"
	"r0 = *(u64 *)(r6 - 8);"
	"1:"
	"r0 = 0;"
	"exit;"
	:: __imm(bpf_get_prandom_u32)
	: __clobber_all);
}

SEC("socket")
__log_level(2)
__msg("14: (7b) *(u64 *)(r6 -8) = r7         ; def: fp0-16")
__msg("15: (79) r0 = *(u64 *)(r6 -8)         ; use: fp0-16")
__naked void callback_write_before_read_kills(void)
{
	asm volatile (
	"r1 = 42;"
	"*(u64 *)(r10 - 8) = r1;"
	"*(u64 *)(r10 - 16) = r1;"
	"r1 = 2;"
	"r2 = cb_write_read ll;"
	"r3 = r10;"
	"r3 += -8;"
	"r4 = 0;"
	"call %[bpf_loop];"
	"r0 = 0;"
	"exit;"
	:: __imm(bpf_loop)
	: __clobber_all);
}

/* Callback unconditionally writes *(ctx-8), then reads it back.
 * The write (parent_def) kills liveness before entry.
 */
static __used __naked void cb_write_read(void)
{
	asm volatile (
	"r6 = r2;"
	"r7 = 99;"
	"*(u64 *)(r6 - 8) = r7;"
	"r0 = *(u64 *)(r6 - 8);"
	"r0 = 0;"
	"exit;"
	::: __clobber_all);
}

/*
 * bpf_loop callback conditionally writes fp-16 then unconditionally
 * reads it. The conditional write does NOT kill liveness
 */
SEC("socket")
__log_level(2)
__msg("9: (85) call bpf_loop#181            ; use: fp0-16")
__naked void callback_conditional_write_preserves(void)
{
	asm volatile (
	"r1 = 42;"
	"*(u64 *)(r10 - 8) = r1;"
	"*(u64 *)(r10 - 16) = r1;"
	"r1 = 2;"
	"r2 = cb_cond_write_read ll;"
	"r3 = r10;"
	"r3 += -8;"
	"r4 = 0;"
	"call %[bpf_loop];"
	"r0 = 0;"
	"exit;"
	:: __imm(bpf_loop)
	: __clobber_all);
}

static __used __naked void cb_cond_write_read(void)
{
	asm volatile (
	"r6 = r2;"
	"call %[bpf_get_prandom_u32];"
	"if r0 == 0 goto 1f;"
	"*(u64 *)(r6 - 8) = r0;"
	"1:"
	"r0 = *(u64 *)(r6 - 8);"
	"r0 = 0;"
	"exit;"
	:: __imm(bpf_get_prandom_u32)
	: __clobber_all);
}

/*
 * Two bpf_loop calls with the same callback but different ctx pointers.
 *
 * First call: ctx=fp-8, second call: ctx=fp-24.
 */
SEC("socket")
__log_level(2)
__msg(" 8: (85) call bpf_loop{{.*}}            ; use: fp0-8")
__msg("15: (85) call bpf_loop{{.*}}            ; use: fp0-24")
__naked void callback_two_calls_different_ctx(void)
{
	asm volatile (
	"*(u64 *)(r10 - 8) = 0;"
	"*(u64 *)(r10 - 24) = 0;"
	"r1 = 1;"
	"r2 = cb_read_ctx ll;"
	"r3 = r10;"
	"r3 += -8;"
	"r4 = 0;"
	"call %[bpf_loop];"
	"r1 = 1;"
	"r2 = cb_read_ctx ll;"
	"r3 = r10;"
	"r3 += -24;"
	"r4 = 0;"
	"call %[bpf_loop];"
	"r0 = 0;"
	"exit;"
	:: __imm(bpf_loop)
	: __clobber_all);
}

/* Callback reads at ctx+0 unconditionally */
static __used __naked void cb_read_ctx(void)
{
	asm volatile (
	"r0 = *(u64 *)(r2 + 0);"
	"r0 = 0;"
	"exit;"
	::: __clobber_all);
}

/*
 * Reproducer for unsound pruning in refined_caller_live_stack().
 *
 * Three-level call chain: main → mid_fwd → grandchild_deref.
 * Main passes &fp-8 to mid_fwd, which forwards R1 to grandchild_deref.
 * grandchild_deref reads main's fp-8 through the forwarded pointer
 * and dereferences the result.
 *
 * refined_caller_live_stack() has a callee_offset++ when mid_fwd
 * (frame 1) is mid-call. This drops the transitive parent_live
 * contribution at mid_fwd's call instruction — the only place
 * where grandchild_deref's read of main's fp-8 is recorded.
 * As a result, main's fp-8 is cleaned to STACK_INVALID at the
 * pruning point inside grandchild_deref, and path B is
 * incorrectly pruned against path A.
 *
 * Path A: main stores PTR_TO_MAP_VALUE at fp-8
 * Path B: main stores scalar 42 at fp-8
 *
 * Correct behavior: reject (path B dereferences scalar)
 * Bug behavior: accept (path B pruned against cleaned path A)
 */
SEC("socket")
__flag(BPF_F_TEST_STATE_FREQ)
__failure __msg("R0 invalid mem access 'scalar'")
__naked void transitive_parent_stack_read_unsound(void)
{
	asm volatile (
	/* Map lookup to get PTR_TO_MAP_VALUE */
	"r1 = %[map] ll;"
	"*(u32 *)(r10 - 32) = 0;"
	"r2 = r10;"
	"r2 += -32;"
	"call %[bpf_map_lookup_elem];"
	"if r0 == 0 goto l_exit%=;"
	"r6 = r0;"
	/* Branch: path A (fall-through) explored first */
	"call %[bpf_get_prandom_u32];"
	"if r0 != 0 goto l_scalar%=;"
	/* Path A: fp-8 = PTR_TO_MAP_VALUE */
	"*(u64 *)(r10 - 8) = r6;"
	"goto l_merge%=;"
"l_scalar%=:"
	/* Path B: fp-8 = scalar 42 */
	"r1 = 42;"
	"*(u64 *)(r10 - 8) = r1;"
"l_merge%=:"
	/* Pass &fp-8 to mid_fwd → grandchild_deref */
	"r1 = r10;"
	"r1 += -8;"
	"call mid_fwd;"
	"r0 = 0;"
	"exit;"
"l_exit%=:"
	"r0 = 0;"
	"exit;"
	:
	: __imm(bpf_map_lookup_elem),
	  __imm(bpf_get_prandom_u32),
	  __imm_addr(map)
	: __clobber_all);
}

/* Forwards R1 (ptr to main's fp-8) to grandchild_deref */
static __used __naked void mid_fwd(void)
{
	asm volatile (
	"call grandchild_deref;"
	"r0 = 0;"
	"exit;"
	::: __clobber_all);
}

/* Reads main's fp-8 through forwarded pointer, dereferences result */
static __used __naked void grandchild_deref(void)
{
	asm volatile (
	"goto +0;"				/* checkpoint */
	"goto +0;"				/* checkpoint */
	/* read main's fp-8: map_ptr (path A) or scalar (path B) */
	"r0 = *(u64 *)(r1 + 0);"
	/* dereference: safe for map_ptr, unsafe for scalar */
	"r0 = *(u64 *)(r0 + 0);"
	"r0 = 0;"
	"exit;"
	::: __clobber_all);
}

SEC("socket")
__log_level(2)
__success
__msg("14: (79) r1 = *(u64 *)(r10 -8) // r6=fp0-8 r7=fp1-16 fp-8=fp1-16 fp-16=fp0-8")
__msg("15: (79) r0 = *(u64 *)(r1 +0) // r1=fp1-16 r6=fp0-8 r7=fp1-16 fp-8=fp1-16 fp-16=fp0-8")
__msg("stack use/def subprog#1 mid_two_fp_threshold (d1,cs2):")
__msg("14: (79) r1 = *(u64 *)(r10 -8)        ; use: fp1-8")
__msg("15: (79) r0 = *(u64 *)(r1 +0)         ; use: fp1-16")
__naked void two_fp_clear_stack_threshold(void)
{
	asm volatile (
	"r1 = r10;"
	"r1 += -8;"
	"call mid_two_fp_threshold;"
	"r0 = 0;"
	"exit;"
	::: __clobber_all);
}

static __used __naked void mid_two_fp_threshold(void)
{
	asm volatile (
	"r6 = r1;"
	"r7 = r10;"
	"r7 += -16;"
	"*(u64 *)(r10 - 8) = r7;"
	"*(u64 *)(r10 - 16) = r6;"
	"r1 = r10;"
	"r1 += -8;"
	"r2 = r6;"
	"call inner_nop_fptest;"
	"r1 = *(u64 *)(r10 - 8);"
	"r0 = *(u64 *)(r1 + 0);"
	"r0 = 0;"
	"exit;"
	::: __clobber_all);
}

static __used __naked void inner_nop_fptest(void)
{
	asm volatile (
	"r0 = 0;"
	"exit;"
	::: __clobber_all);
}

SEC("socket")
__log_level(2)
__success
__msg("13: (79) r1 = *(u64 *)(r10 -8) // r6=fp0-8 r7=fp1-16 fp-8=fp1-16 fp-16=fp0-8")
__msg("14: (79) r0 = *(u64 *)(r1 +0) // r1=fp1-16 r6=fp0-8 r7=fp1-16 fp-8=fp1-16 fp-16=fp0-8")
__msg("stack use/def subprog#1 mid_one_fp_threshold (d1,cs2):")
__msg("13: (79) r1 = *(u64 *)(r10 -8)        ; use: fp1-8")
__msg("14: (79) r0 = *(u64 *)(r1 +0)         ; use: fp1-16")
__naked void one_fp_clear_stack_threshold(void)
{
	asm volatile (
	"r1 = r10;"
	"r1 += -8;"
	"call mid_one_fp_threshold;"
	"r0 = 0;"
	"exit;"
	::: __clobber_all);
}

static __used __naked void mid_one_fp_threshold(void)
{
	asm volatile (
	"r6 = r1;"
	"r7 = r10;"
	"r7 += -16;"
	"*(u64 *)(r10 - 8) = r7;"
	"*(u64 *)(r10 - 16) = r6;"
	"r1 = r10;"
	"r1 += -8;"
	"call inner_nop_fptest;"
	"r1 = *(u64 *)(r10 - 8);"
	"r0 = *(u64 *)(r1 + 0);"
	"r0 = 0;"
	"exit;"
	::: __clobber_all);
}

/*
 * Reproducer for unsound pruning when a subprog forwards a parent
 * stack pointer (AT_PARENT) to a helper with a memory argument.
 *
 * set_call_stack_access_at() previously only tracked AT_CURRENT args,
 * skipping AT_PARENT entirely. This meant helper reads through parent
 * stack pointers did not set parent_use, letting the slot appear dead
 * at pruning checkpoints inside the subprog.
 *
 * Program shape:
 *   main:
 *     *(u32)(fp-4) = 0             key = STACK_ZERO (const 0)
 *     call bpf_get_prandom_u32
 *     if r0 != 0 goto clobber      path A (fall-through) first
 *     goto merge
 *   clobber:
 *     *(u8)(fp-4) = r0             path B: key[0] = STACK_MISC
 *   merge:
 *     r1 = fp - 4
 *     call fwd_parent_key_to_helper
 *     r0 = 0
 *     exit
 *
 *   fwd_parent_key_to_helper(r1 = &caller_fp-4):
 *     goto +0                      checkpoint
 *     r2 = r1                      R2 = AT_PARENT ptr to caller fp-4
 *     r1 = array_map_8b ll         R1 = array map
 *     call bpf_map_lookup_elem     reads key_size(4) from parent fp-4
 *     r0 = *(u64 *)(r0 + 0)        deref without null check
 *     r0 = 0
 *     exit
 *
 * Path A: STACK_ZERO key = const 0 -> array lookup -> PTR_TO_MAP_VALUE
 *         (non-NULL for in-bounds const key) -> deref OK.
 * Path B: STACK_MISC key = unknown -> array lookup ->
 *         PTR_TO_MAP_VALUE_OR_NULL -> deref UNSAFE.
 *
 * Bug: AT_PARENT R2 arg to bpf_map_lookup_elem skipped -> parent_use
 *      not set -> fp-4 cleaned at checkpoint -> STACK_ZERO collapses
 *      to STACK_INVALID -> path B pruned -> deref never checked.
 *
 * Correct verifier behavior: reject (path B deref of map_value_or_null).
 */
SEC("socket")
__flag(BPF_F_TEST_STATE_FREQ)
__failure __msg("R0 invalid mem access 'map_value_or_null'")
__naked void helper_parent_stack_read_unsound(void)
{
	asm volatile (
	/* key at fp-4: all bytes STACK_ZERO */
	"*(u32 *)(r10 - 4) = 0;"
	"call %[bpf_get_prandom_u32];"
	/* fall-through (path A) explored first */
	"if r0 != 0 goto l_clobber%=;"
	/* path A: key stays constant zero */
	"goto l_merge%=;"
"l_clobber%=:"
	/* path B: key[0] becomes STACK_MISC, key no longer const */
	"*(u8 *)(r10 - 4) = r0;"
"l_merge%=:"
	"r1 = r10;"
	"r1 += -4;"
	"call fwd_parent_key_to_helper;"
	"r0 = 0;"
	"exit;"
	:
	: __imm(bpf_get_prandom_u32)
	: __clobber_all);
}

/*
 * Subprog forwards parent stack pointer to bpf_map_lookup_elem as key
 * on an array map, then dereferences the result without a null check.
 * R1 = &parent_fp-4 (AT_PARENT in this frame).
 *
 * The helper reads key_size(4) bytes from parent stack.  The deref of
 * R0 reads the map value, NOT parent stack, so record_insn_mem_accesses
 * does not set parent_use for it.  The ONLY parent stack access is
 * through the helper's R2 arg.
 */
static __used __naked void fwd_parent_key_to_helper(void)
{
	asm volatile (
	"goto +0;"				/* checkpoint */
	"r2 = r1;"				/* R2 = parent ptr (AT_PARENT) */
	"r1 = %[array_map_8b] ll;"		/* R1 = array map */
	"call %[bpf_map_lookup_elem];"		/* reads 4 bytes from parent fp-4 */
	/* deref without null check: safe for PTR_TO_MAP_VALUE,
	 * unsafe for PTR_TO_MAP_VALUE_OR_NULL
	 */
	"r0 = *(u64 *)(r0 + 0);"
	"r0 = 0;"
	"exit;"
	:
	: __imm(bpf_map_lookup_elem),
	  __imm_addr(array_map_8b)
	: __clobber_all);
}

/*
 * Regression for keeping later helper args after a whole-stack fallback
 * on an earlier local arg.  The first bpf_snprintf() arg is a local
 * frame-derived pointer with offset-imprecise tracking (`fp1 ?`), which
 * conservatively marks the whole local stack live.  The fourth arg still
 * forwards &parent_fp-8 and must contribute nonlocal_use[0]=0:3.
 */
SEC("socket")
__log_level(2)
__success
__msg("call bpf_snprintf{{.*}}        ; use: fp1-8..-512 fp0-8")
__naked void helper_arg_fallback_keeps_scanning(void)
{
	asm volatile (
	"r1 = 42;"
	"*(u64 *)(r10 - 8) = r1;"
	"r1 = r10;"
	"r1 += -8;"
	"call helper_snprintf_parent_after_local_fallback;"
	"r0 = 0;"
	"exit;"
	::: __clobber_all);
}

static __used __naked void helper_snprintf_parent_after_local_fallback(void)
{
	asm volatile (
	"r6 = r1;"				/* save &parent_fp-8 */
	"call %[bpf_get_prandom_u32];"
	"r0 &= 8;"
	"r1 = r10;"
	"r1 += -16;"
	"r1 += r0;"				/* local fp, offset-imprecise */
	"r2 = 8;"
	"r3 = %[snprintf_u64_fmt] ll;"
	"r4 = r6;"				/* later arg: parent fp-8 */
	"r5 = 8;"
	"call %[bpf_snprintf];"
	"r0 = 0;"
	"exit;"
	:
	: __imm(bpf_get_prandom_u32),
	  __imm(bpf_snprintf),
	  __imm_addr(snprintf_u64_fmt)
	: __clobber_all);
}

/*
 * Test that propagate_callee_ancestor() correctly chains ancestor
 * liveness across sequential calls within a single frame.
 *
 * main → mid_seq_touch → {nop_callee, deref_ancestor}
 *
 * mid_seq_touch receives two pointers: R1 = &main_fp-8 (forwarded to
 * deref_ancestor) and R2 = &main_fp-16 (read directly by mid_seq_touch).
 * The direct read of fp-16 forces ensure_anc_arrays() to allocate
 * ancestor_live[0] for mid_seq_touch, so refined_caller_live_stack()
 * uses the refined path (not the conservative fallback).
 *
 * mid_seq_touch calls nop_callee first (no-op, creates a pruning point),
 * then calls deref_ancestor which reads main's fp-8 and dereferences it.
 *
 * propagate_callee_ancestor() propagates deref_ancestor's entry
 * ancestor_live[0] into mid_seq_touch's anc_use[0] at the call-to-deref
 * instruction.  mid_seq_touch's backward pass flows this backward so
 * ancestor_live[0] includes fp-8 at the pruning point between the calls.
 *
 * Without propagation, mid_seq_touch's ancestor_live[0] only has fp-16
 * (from the direct read) — fp-8 is missing.  refined_caller_live_stack()
 * Term 1 says fp-8 is dead, the verifier cleans it, and path B
 * (scalar 42) is incorrectly pruned against path A (MAP_VALUE).
 *
 * Path A: main stores PTR_TO_MAP_VALUE at fp-8  → deref succeeds
 * Path B: main stores scalar 42 at fp-8         → deref must fail
 *
 * Correct: reject (path B dereferences scalar)
 */
SEC("socket")
__flag(BPF_F_TEST_STATE_FREQ)
__failure __msg("R0 invalid mem access 'scalar'")
__naked void propagate_callee_ancestor_chain(void)
{
	asm volatile (
	/* Map lookup to get PTR_TO_MAP_VALUE */
	"r1 = %[map] ll;"
	"*(u32 *)(r10 - 32) = 0;"
	"r2 = r10;"
	"r2 += -32;"
	"call %[bpf_map_lookup_elem];"
	"if r0 == 0 goto l_exit%=;"
	"r6 = r0;"
	/* Branch: path A (fall-through) explored first */
	"call %[bpf_get_prandom_u32];"
	"if r0 != 0 goto l_scalar%=;"
	/* Path A: fp-8 = PTR_TO_MAP_VALUE */
	"*(u64 *)(r10 - 8) = r6;"
	"goto l_merge%=;"
"l_scalar%=:"
	/* Path B: fp-8 = scalar 42 */
	"r1 = 42;"
	"*(u64 *)(r10 - 8) = r1;"
"l_merge%=:"
	/* fp-16 = dummy value (mid_seq_touch reads it directly) */
	"r1 = 99;"
	"*(u64 *)(r10 - 16) = r1;"
	/* R1 = &fp-8 (for deref_ancestor), R2 = &fp-16 (for mid_seq_touch) */
	"r1 = r10;"
	"r1 += -8;"
	"r2 = r10;"
	"r2 += -16;"
	"call mid_seq_touch;"
	"r0 = 0;"
	"exit;"
"l_exit%=:"
	"r0 = 0;"
	"exit;"
	:
	: __imm(bpf_map_lookup_elem),
	  __imm(bpf_get_prandom_u32),
	  __imm_addr(map)
	: __clobber_all);
}

/*
 * R1 = &main_fp-8 (forwarded to deref_ancestor)
 * R2 = &main_fp-16 (read directly here → allocates ancestor_live[0])
 *
 * Reads main's fp-16 to force ancestor_live[0] allocation, then
 * calls nop_callee (pruning point), then deref_ancestor.
 */
static __used __naked void mid_seq_touch(void)
{
	asm volatile (
	"r6 = r1;"			/* save &main_fp-8 in callee-saved */
	"r0 = *(u64 *)(r2 + 0);"	/* read main's fp-16: triggers anc_use[0] */
	"call nop_callee;"		/* no-op, creates pruning point after */
	"r1 = r6;"			/* restore ptr to &main_fp-8 */
	"call deref_ancestor;"		/* reads main's fp-8, dereferences */
	"r0 = 0;"
	"exit;"
	::: __clobber_all);
}

static __used __naked void nop_callee(void)
{
	asm volatile (
	"r0 = 0;"
	"exit;"
	::: __clobber_all);
}

/* Reads main's fp-8 through forwarded pointer, dereferences result */
static __used __naked void deref_ancestor(void)
{
	asm volatile (
	"r0 = *(u64 *)(r1 + 0);"	/* read main's fp-8 */
	"r0 = *(u64 *)(r0 + 0);"	/* deref: safe for map_ptr, unsafe for scalar */
	"r0 = 0;"
	"exit;"
	::: __clobber_all);
}

/*
 * Test: callee loads an fp-derived pointer from caller's stack, then
 * reads through it to access another caller stack slot.
 *
 * main stores PTR_TO_MAP_VALUE at fp-24, stores &fp-24 (an fp-derived
 * pointer) at fp-8, passes &fp-8 through mid_fwd_spilled_ptr to
 * load_ptr_deref_grandchild.  The leaf loads the pointer from main's
 * fp-8, then reads main's fp-24 through the loaded pointer.
 *
 * fill_from_stack() in arg_track_xfer() only handles local-frame
 * FP-derived loads (src_is_local_fp check requires frame == depth).
 * When a callee loads from a parent-frame pointer (frame < depth),
 * the loaded value gets ARG_NONE instead of being recognized as
 * fp-derived.  Subsequent reads through that loaded pointer are
 * invisible to liveness — nonlocal_use is never set for fp-24.
 *
 * clean_live_states() cleans the current state at every prune point.
 * Because liveness misses fp-24, refined_caller_live_stack() tells
 * __clean_func_state() that fp-24 is dead, which destroys the
 * PTR_TO_MAP_VALUE spill before the grandchild can read it.
 * The grandchild then reads STACK_INVALID → scalar, and the deref
 * is rejected with "R0 invalid mem access 'scalar'" — even though
 * fp-24 is genuinely live and holds a valid map pointer.
 *
 * This is a false positive: a valid program incorrectly rejected.
 */
SEC("socket")
__flag(BPF_F_TEST_STATE_FREQ)
__success
__naked void spilled_fp_cross_frame_deref(void)
{
	asm volatile (
	/* Map lookup to get PTR_TO_MAP_VALUE */
	"r1 = %[map] ll;"
	"*(u32 *)(r10 - 32) = 0;"
	"r2 = r10;"
	"r2 += -32;"
	"call %[bpf_map_lookup_elem];"
	"if r0 == 0 goto l_exit%=;"
	/* fp-24 = PTR_TO_MAP_VALUE */
	"*(u64 *)(r10 - 24) = r0;"
	/* Store pointer to fp-24 at fp-8 */
	"r1 = r10;"
	"r1 += -24;"
	"*(u64 *)(r10 - 8) = r1;"
	/* R1 = &fp-8: pointer to the spilled ptr */
	"r1 = r10;"
	"r1 += -8;"
	"call mid_fwd_spilled_ptr;"
	"r0 = 0;"
	"exit;"
"l_exit%=:"
	"r0 = 0;"
	"exit;"
	:
	: __imm(bpf_map_lookup_elem),
	  __imm_addr(map)
	: __clobber_all);
}

/* Forwards R1 (ptr to main's fp-8, which holds &main_fp-24) to leaf */
static __used __naked void mid_fwd_spilled_ptr(void)
{
	asm volatile (
	"call load_ptr_deref_grandchild;"
	"r0 = 0;"
	"exit;"
	::: __clobber_all);
}

/*
 * R1 = &main_fp-8 (where main stored ptr to fp-24)
 * Loads the ptr from main's fp-8, reads main's fp-24 through it,
 * then dereferences the result.
 */
static __used __naked void load_ptr_deref_grandchild(void)
{
	asm volatile (
	/* Load ptr from main's fp-8 → r2 = &main_fp-24 */
	"r2 = *(u64 *)(r1 + 0);"
	/* Read main's fp-24 through loaded ptr */
	"r0 = *(u64 *)(r2 + 0);"
	/* Dereference: safe for map_ptr */
	"r0 = *(u64 *)(r0 + 0);"
	"r0 = 0;"
	"exit;"
	::: __clobber_all);
}

/*
 * Exercise merge_nonlocal_live().
 *
 * merge_shared_mid is analyzed twice (once from each wrapper), so the
 * callsite within merge_shared_mid that calls merge_leaf_read gets its
 * nonlocal_live info merged twice via merge_nonlocal_live().
 */
SEC("socket")
__log_level(2)
__success
__msg("14: (85) call pc+2	r1: fp0-16")
__msg("17: (79) r0 = *(u64 *)(r1 +0) // r1=fp0-16")
__msg("14: (85) call pc+2	r1: fp0-8")
__msg("17: (79) r0 = *(u64 *)(r1 +0) // r1=fp0-8")
__msg("5: (85) call pc+{{.*}}                   ; use: fp0-8 fp0-16")
__naked void test_merge_nonlocal_live(void)
{
	asm volatile (
	"r1 = 0;"
	"*(u64 *)(r10 - 8) = r1;"
	"*(u64 *)(r10 - 16) = r1;"
	"r1 = r10;"
	"r1 += -8;"
	"call merge_wrapper_a;"
	"r1 = r10;"
	"r1 += -16;"
	"call merge_wrapper_b;"
	"r0 = 0;"
	"exit;"
	::: __clobber_all);
}

static __used __naked void merge_wrapper_a(void)
{
	asm volatile (
	"call merge_shared_mid;"
	"r0 = 0;"
	"exit;"
	::: __clobber_all);
}

static __used __naked void merge_wrapper_b(void)
{
	asm volatile (
	"call merge_shared_mid;"
	"r0 = 0;"
	"exit;"
	::: __clobber_all);
}

static __used __naked void merge_shared_mid(void)
{
	asm volatile (
	"call merge_leaf_read;"
	"r0 = 0;"
	"exit;"
	::: __clobber_all);
}

static __used __naked void merge_leaf_read(void)
{
	asm volatile (
	"r0 = *(u64 *)(r1 + 0);"
	"r0 = 0;"
	"exit;"
	::: __clobber_all);
}

/* Same bpf_loop instruction calls different callbacks depending on branch. */
SEC("socket")
__log_level(2)
__success
__msg("call bpf_loop#181            ; use: fp2-8..-512 fp1-8..-512 fp0-8..-512")
__naked void bpf_loop_two_callbacks(void)
{
	asm volatile (
	"r1 = 0;"
	"*(u64 *)(r10 - 8) = r1;"
	"*(u64 *)(r10 - 16) = r1;"
	"r1 = r10;"
	"r1 += -8;"
	"call dyn_wrapper_a;"
	"r1 = r10;"
	"r1 += -16;"
	"call dyn_wrapper_b;"
	"r0 = 0;"
	"exit;"
	::: __clobber_all);
}

static __used __naked void dyn_wrapper_a(void)
{
	asm volatile (
	"call mid_dynamic_cb;"
	"r0 = 0;"
	"exit;"
	::: __clobber_all);
}

static __used __naked void dyn_wrapper_b(void)
{
	asm volatile (
	"call mid_dynamic_cb;"
	"r0 = 0;"
	"exit;"
	::: __clobber_all);
}

static __used __naked void mid_dynamic_cb(void)
{
	asm volatile (
	"r6 = r1;"
	"call %[bpf_get_prandom_u32];"
	"if r0 == 0 goto 1f;"
	"r2 = dyn_cb_a ll;"
	"goto 2f;"
	"1:"
	"r2 = dyn_cb_b ll;"
	"2:"
	"r1 = 1;"
	"r3 = r6;" /* ctx = fp-derived ptr from parent */
	"r4 = 0;"
	"call %[bpf_loop];"
	"r0 = 0;"
	"exit;"
	:: __imm(bpf_get_prandom_u32),
	   __imm(bpf_loop)
	: __clobber_all);
}

/* Callback A/B: read parent stack through ctx */
static __used __naked void dyn_cb_a(void)
{
	asm volatile (
	"r0 = *(u64 *)(r2 + 0);"
	"r0 = 0;"
	"exit;"
	::: __clobber_all);
}

static __used __naked void dyn_cb_b(void)
{
	asm volatile (
	"r0 = *(u64 *)(r2 + 0);"
	"r0 = 0;"
	"exit;"
	::: __clobber_all);
}

/*
 * Path A: r0 = map_lookup result (non-FP, ARG_NONE for stack tracking)
 * Path B: r0 = fp-8 (FP-derived, frame=0, off=-8)
 * At the join: r0 is not guaranteed to be a frame pointer.
 */
SEC("socket")
__log_level(2)
__msg("10: (79) r0 = *(u64 *)(r10 -8) // r0=fp0-8|fp0+0")
__naked void stack_or_non_stack_write(void)
{
	asm volatile (
	/* initial write to fp-8 */
	"*(u64 *)(r10 - 8) = 0;"
	/* map lookup to get a non-FP pointer */
	"r2 = r10;"
	"r2 += -4;"
	"r1 = %[map] ll;"
	"call %[bpf_map_lookup_elem];"
	/* r0 = map_value (ARG_NONE) */
	"if r0 != 0 goto 1f;"
	/* path B: r0 = fp-8 */
	"r0 = r10;"
	"r0 += -8;"
"1:"
	/* join: the write is not a def for fp[0]-8 */
	"*(u64 *)(r0 + 0) = 7;"
	/* read fp-8: should be non-poisoned */
	"r0 = *(u64 *)(r10 - 8);"
	"exit;"
	:
	: __imm(bpf_map_lookup_elem),
	  __imm_addr(map)
	: __clobber_all);
}

SEC("socket")
__log_level(2)
__flag(BPF_F_TEST_STATE_FREQ)
__msg("subprog#2 write_first_read_second:")
__msg("17: (7a) *(u64 *)(r1 +0) = 42{{$}}")
__msg("18: (79) r0 = *(u64 *)(r2 +0) // r1=fp0-8 r2=fp0-16{{$}}")
__msg("stack use/def subprog#2 write_first_read_second (d2,cs15):")
__msg("17: (7a) *(u64 *)(r1 +0) = 42{{$}}")
__msg("18: (79) r0 = *(u64 *)(r2 +0)         ; use: fp0-8 fp0-16")
__naked void shared_instance_must_write_overwrite(void)
{
	asm volatile (
	"r1 = 1;"
	"*(u64 *)(r10 - 8) = r1;"
	"*(u64 *)(r10 - 16) = r1;"
	/* Call 1: write_first_read_second(&fp[-8], &fp[-16]) */
	"r1 = r10;"
	"r1 += -8;"
	"r2 = r10;"
	"r2 += -16;"
	"call forwarding_rw;"
	/* Call 2: write_first_read_second(&fp[-16], &fp[-8]) */
	"r1 = r10;"
	"r1 += -16;"
	"r2 = r10;"
	"r2 += -8;"
	"call forwarding_rw;"
	"r0 = 0;"
	"exit;"
	::: __clobber_all);
}

static __used __naked void forwarding_rw(void)
{
	asm volatile (
	"call write_first_read_second;"
	"exit;"
	::: __clobber_all);
}

static __used __naked void write_first_read_second(void)
{
	asm volatile (
	"*(u64 *)(r1 + 0) = 42;"
	"r0 = *(u64 *)(r2 + 0);"
	"exit;"
	::: __clobber_all);
}

/*
 * Shared must_write when (callsite, depth) instance is reused.
 * Main calls fwd_to_stale_wr at two sites. fwd_to_stale_wr calls
 * stale_wr_leaf at a single internal callsite. Both calls share
 * stale_wr_leaf's (callsite, depth) instance.
 *
 * Call 1: stale_wr_leaf(map_value, fp-8) writes map, reads fp-8.
 * Call 2: stale_wr_leaf(fp-8, fp-8) writes fp-8, reads fp-8.
 *
 * The analysis can't presume that stale_wr_leaf() always writes fp-8,
 * it must conservatively join must_write masks computed for both calls.
 */
SEC("socket")
__success
__naked void stale_must_write_cross_callsite(void)
{
	asm volatile (
	"*(u64 *)(r10 - 8) = 0;"
	/* Call 1: map_value write, fp-8 read (processed second in PO) */
	"*(u32 *)(r10 - 16) = 0;"
	"r1 = %[map] ll;"
	"r2 = r10;"
	"r2 += -16;"
	"call %[bpf_map_lookup_elem];"
	"if r0 == 0 goto 1f;"
	"r1 = r0;"
	"r2 = r10;"
	"r2 += -8;"
	"call fwd_to_stale_wr;"
	/* Call 2: fp-8 write, fp-8 read (processed first in PO) */
	"r1 = r10;"
	"r1 += -8;"
	"r2 = r1;"
	"call fwd_to_stale_wr;"
"1:"
	"r0 = 0;"
	"exit;"
	:: __imm_addr(map),
	   __imm(bpf_map_lookup_elem)
	: __clobber_all);
}

static __used __naked void fwd_to_stale_wr(void)
{
	asm volatile (
	"call stale_wr_leaf;"
	"exit;"
	::: __clobber_all);
}

static __used __naked void stale_wr_leaf(void)
{
	asm volatile (
	"*(u64 *)(r1 + 0) = 42;"
	"r0 = *(u64 *)(r2 + 0);"
	"exit;"
	::: __clobber_all);
}

#ifdef CAN_USE_LOAD_ACQ_STORE_REL

SEC("socket")
__log_level(2)
__success
__msg("*(u64 *)(r0 +0) = 42         ; def: fp0-16")
__naked void load_acquire_dont_clear_dst(void)
{
	asm volatile (
	"r0 = r10;"
	"r0 += -16;"
	"*(u64 *)(r0 + 0) = r0;"	/* fp[-16] == &fp[-16] */
	".8byte %[load_acquire_insn];"	/* load_acquire is a special case for BPF_STX, */
	"r0 = *(u64 *)(r10 - 16);"	/* it shouldn't clear tracking info for */
	"*(u64 *)(r0 + 0) = 42;"	/* dst register, r0 in this case. */
	"r0 = 0;"
	"exit;"
	:
	: __imm_insn(load_acquire_insn,
		     BPF_ATOMIC_OP(BPF_DW, BPF_LOAD_ACQ, BPF_REG_0, BPF_REG_0, 0))
	: __clobber_all);
}

#endif /* CAN_USE_LOAD_ACQ_STORE_REL */

SEC("socket")
__success
__naked void imprecise_fill_loses_cross_frame(void)
{
	asm volatile (
	"*(u64 *)(r10 - 8) = 0;"
	"r1 = r10;"
	"r1 += -8;"
	"call imprecise_fill_cross_frame;"
	"exit;"
	::: __clobber_all);
}

static __used __naked void imprecise_fill_cross_frame(void)
{
	asm volatile (
	/* spill &caller_fp-8 to callee's fp-8 */
	"*(u64 *)(r10 - 8) = r1;"
	/* imprecise FP pointer in r1 */
	"r1 = r10;"
	"r2 = -8;"
	"r1 += r2;"
	/* load from imprecise offset. fill_from_stack returns
	 * ARG_IMPRECISE{mask=BIT(1)}, losing frame 0
	 */
	"r1 = *(u64 *)(r1 + 0);"
	/* read caller's fp-8 through loaded pointer, should mark fp0-8 live */
	"r0 = *(u64 *)(r1 + 0);"
	"r0 = 0;"
	"exit;"
	:: __imm(bpf_get_prandom_u32)
	: __clobber_all);
}

/* Test that spill_to_stack with multi-offset dst (sz=8) joins instead
 * of overwriting. r1 has offsets [-8, -16]. Both slots hold FP-derived
 * pointers. Writing through r1 should join *val with existing values,
 * not destroy them.
 *
 *   fp-8  = &fp-24
 *   fp-16 = &fp-32
 *   r1 = fp-8 or fp-16 (two offsets from branch)
 *   *(u64 *)(r1 + 0) = &fp-24   -- writes to one slot, other untouched
 *   r0 = *(u64 *)(r10 - 16)     -- fill from fp-16
 *   r0 = *(u64 *)(r0 + 0)       -- deref: should produce use
 */
SEC("socket")
__log_level(2)
__success
__msg("20: (79) r0 = *(u64 *)(r10 -16)")
__msg("21: (79) r0 = *(u64 *)(r0 +0)         ; use: fp0-24 fp0-32")
__naked void spill_join_with_multi_off(void)
{
	asm volatile (
	/* fp-8 = &fp-24, fp-16 = &fp-32 (different pointers) */
	"*(u64 *)(r10 - 24) = 0;"
	"*(u64 *)(r10 - 32) = 0;"
	"r1 = r10;"
	"r1 += -24;"
	"*(u64 *)(r10 - 8) = r1;"
	"r1 = r10;"
	"r1 += -32;"
	"*(u64 *)(r10 - 16) = r1;"
	/* create r1 with two candidate offsets: fp-8 or fp-16 */
	"call %[bpf_get_prandom_u32];"
	"if r0 == 0 goto 1f;"
	"r1 = r10;"
	"r1 += -8;"
	"goto 2f;"
"1:"
	"r1 = r10;"
	"r1 += -16;"
"2:"
	/* write &fp-24 through multi-offset r1: hits one slot, other untouched */
	"r2 = r10;"
	"r2 += -24;"
	"*(u64 *)(r1 + 0) = r2;"
	/* read back *fp-8 and *fp-16 */
	"r0 = *(u64 *)(r10 - 8);"
	"r0 = *(u64 *)(r0 + 0);"
	"r0 = *(u64 *)(r10 - 16);"
	"r0 = *(u64 *)(r0 + 0);"
	"exit;"
	:: __imm(bpf_get_prandom_u32)
	: __clobber_all);
}

/* Test that spill_to_stack with imprecise dst (off_cnt == 0, sz=8)
 * joins instead of overwriting. Use "r2 = -8; r1 += r2" to make
 * arg tracking lose offset precision while the main verifier keeps
 * r1 as PTR_TO_STACK with fixed offset. Both slots hold FP-derived
 * pointers. Writing through r1 should join *val with existing
 * values, not destroy them.
 *
 *   fp-8  = &fp-24
 *   fp-16 = &fp-32
 *   r1 = fp-8 (imprecise to arg tracking)
 *   *(u64 *)(r1 + 0) = &fp-24   -- since r1 is imprecise, this adds &fp-24
 *                                  to the set of possible values for all slots,
 *                                  hence the values at fp-16 become [fp-24, fp-32]
 *   r0 = *(u64 *)(r10 - 16)
 *   r0 = *(u64 *)(r0 + 0)       -- deref: should produce use of fp-24 or fp-32
 */
SEC("socket")
__log_level(2)
__success
__msg("15: (79) r0 = *(u64 *)(r0 +0)         ; use: fp0-24 fp0-32")
__naked void spill_join_with_imprecise_off(void)
{
	asm volatile (
	"*(u64 *)(r10 - 24) = 0;"
	"*(u64 *)(r10 - 32) = 0;"
	"r1 = r10;"
	"r1 += -24;"
	"*(u64 *)(r10 - 8) = r1;"
	"r1 = r10;"
	"r1 += -32;"
	"*(u64 *)(r10 - 16) = r1;"
	/* r1 = fp-8 but arg tracking sees off_cnt == 0 */
	"r1 = r10;"
	"r2 = -8;"
	"r1 += r2;"
	/* write through imprecise r1 */
	"r3 = r10;"
	"r3 += -24;"
	"*(u64 *)(r1 + 0) = r3;"
	/* read back fp-16: at_stack should still track &fp-32 */
	"r0 = *(u64 *)(r10 - 16);"
	/* deref: should produce use for fp-32 */
	"r0 = *(u64 *)(r0 + 0);"
	"r0 = 0;"
	"exit;"
	::: __clobber_all);
}

/*
 * Same as spill_join_with_multi_off but the write is BPF_ST (store
 * immediate) instead of BPF_STX. BPF_ST goes through
 * clear_stack_for_all_offs() rather than spill_to_stack(), and that
 * path also needs to join instead of overwriting.
 *
 *   fp-8  = &fp-24
 *   fp-16 = &fp-32
 *   r1 = fp-8 or fp-16 (two offsets from branch)
 *   *(u64 *)(r1 + 0) = 0        -- BPF_ST with immediate
 *   r0 = *(u64 *)(r10 - 16)     -- fill from fp-16
 *   r0 = *(u64 *)(r0 + 0)       -- deref: should produce use
 */
SEC("socket")
__log_level(2)
__failure
__msg("15: (7a) *(u64 *)(r1 +0) = 0	fp-8: fp0-24 -> fp0-24|fp0+0	fp-16: fp0-32 -> fp0-32|fp0+0")
__msg("17: (79) r0 = *(u64 *)(r0 +0)         ; use: fp0-32")
__naked void st_imm_join_with_multi_off(void)
{
	asm volatile (
	"*(u64 *)(r10 - 24) = 0;"
	"*(u64 *)(r10 - 32) = 0;"
	"r1 = r10;"
	"r1 += -24;"
	"*(u64 *)(r10 - 8) = r1;"
	"r1 = r10;"
	"r1 += -32;"
	"*(u64 *)(r10 - 16) = r1;"
	/* create r1 with two candidate offsets: fp-8 or fp-16 */
	"call %[bpf_get_prandom_u32];"
	"if r0 == 0 goto 1f;"
	"r1 = r10;"
	"r1 += -8;"
	"goto 2f;"
"1:"
	"r1 = r10;"
	"r1 += -16;"
"2:"
	/* BPF_ST: store immediate through multi-offset r1 */
	"*(u64 *)(r1 + 0) = 0;"
	/* read back fp-16 and deref */
	"r0 = *(u64 *)(r10 - 16);"
	"r0 = *(u64 *)(r0 + 0);"
	"r0 = 0;"
	"exit;"
	:: __imm(bpf_get_prandom_u32)
	: __clobber_all);
}

/*
 * Check that BPF_ST with a known offset fully overwrites stack slot
 * from the arg tracking point of view.
 */
SEC("socket")
__log_level(2)
__success
__msg("5: (7a) *(u64 *)(r1 +0) = 0	fp-8: fp0-16 -> _{{$}}")
__naked void st_imm_join_with_single_off(void)
{
	asm volatile (
	"r2 = r10;"
	"r2 += -16;"
	"*(u64 *)(r10 - 8) = r2;"
	"r1 = r10;"
	"r1 += -8;"
	"*(u64 *)(r1 + 0) = 0;"
	"r0 = 0;"
	"exit;"
	::: __clobber_all);
}

/*
 * Same as spill_join_with_imprecise_off but the write is BPF_ST.
 * Use "r2 = -8; r1 += r2" to make arg tracking lose offset
 * precision while the main verifier keeps r1 as fixed-offset.
 *
 *   fp-8  = &fp-24
 *   fp-16 = &fp-32
 *   r1 = fp-8 (imprecise to arg tracking)
 *   *(u64 *)(r1 + 0) = 0        -- BPF_ST with immediate
 *   r0 = *(u64 *)(r10 - 16)     -- fill from fp-16
 *   r0 = *(u64 *)(r0 + 0)       -- deref: should produce use
 */
SEC("socket")
__log_level(2)
__success
__msg("13: (79) r0 = *(u64 *)(r0 +0)         ; use: fp0-32")
__naked void st_imm_join_with_imprecise_off(void)
{
	asm volatile (
	"*(u64 *)(r10 - 24) = 0;"
	"*(u64 *)(r10 - 32) = 0;"
	"r1 = r10;"
	"r1 += -24;"
	"*(u64 *)(r10 - 8) = r1;"
	"r1 = r10;"
	"r1 += -32;"
	"*(u64 *)(r10 - 16) = r1;"
	/* r1 = fp-8 but arg tracking sees off_cnt == 0 */
	"r1 = r10;"
	"r2 = -8;"
	"r1 += r2;"
	/* store immediate through imprecise r1 */
	"*(u64 *)(r1 + 0) = 0;"
	/* read back fp-16 */
	"r0 = *(u64 *)(r10 - 16);"
	/* deref: should produce use */
	"r0 = *(u64 *)(r0 + 0);"
	"r0 = 0;"
	"exit;"
	::: __clobber_all);
}

/*
 * Test that spilling through an ARG_IMPRECISE pointer joins with
 * existing at_stack values. Subprog receives r1 = fp0-24 and
 * r2 = map_value, creates an ARG_IMPRECISE pointer by joining caller
 * and callee FP on two branches.
 *
 * Setup: callee spills &fp1-16 to fp1-8 (precise, tracked).
 * Then writes map_value through ARG_IMPRECISE r1 — on path A
 * this hits fp1-8, on path B it hits caller stack.
 * Since spill_to_stack is skipped for ARG_IMPRECISE dst,
 * fp1-8 tracking isn't joined with none.
 *
 * Expected after the imprecise write:
 * - arg tracking should show fp1-8 = fp1-16|fp1+0 (joined with none)
 * - read from fp1-8 and deref should produce use for fp1-16
 * - write through it should NOT produce def for fp1-16
 */
SEC("socket")
__log_level(2)
__success
__msg("26: (79) r0 = *(u64 *)(r10 -8) // r1=IMP3 r6=fp0-24 r7=fp1-16 fp-8=fp1-16|fp1+0")
__naked void imprecise_dst_spill_join(void)
{
	asm volatile (
	"*(u64 *)(r10 - 24) = 0;"
	/* map lookup for a valid non-FP pointer */
	"*(u32 *)(r10 - 32) = 0;"
	"r1 = %[map] ll;"
	"r2 = r10;"
	"r2 += -32;"
	"call %[bpf_map_lookup_elem];"
	"if r0 == 0 goto 1f;"
	/* r1 = &caller_fp-24, r2 = map_value */
	"r1 = r10;"
	"r1 += -24;"
	"r2 = r0;"
	"call imprecise_dst_spill_join_sub;"
"1:"
	"r0 = 0;"
	"exit;"
	:: __imm_addr(map),
	   __imm(bpf_map_lookup_elem)
	: __clobber_all);
}

static __used __naked void imprecise_dst_spill_join_sub(void)
{
	asm volatile (
	/* r6 = &caller_fp-24 (frame=0), r8 = map_value */
	"r6 = r1;"
	"r8 = r2;"
	/* spill &fp1-16 to fp1-8: at_stack[0] = fp1-16 */
	"*(u64 *)(r10 - 16) = 0;"
	"r7 = r10;"
	"r7 += -16;"
	"*(u64 *)(r10 - 8) = r7;"
	/* branch to create ARG_IMPRECISE pointer */
	"call %[bpf_get_prandom_u32];"
	/* path B: r1 = caller fp-24 (frame=0) */
	"r1 = r6;"
	"if r0 == 0 goto 1f;"
	/* path A: r1 = callee fp-8 (frame=1) */
	"r1 = r10;"
	"r1 += -8;"
"1:"
	/* r1 = ARG_IMPRECISE{mask=BIT(0)|BIT(1)}.
	 * Write map_value (non-FP) through r1. On path A this overwrites fp1-8.
	 * Should join at_stack[0] with none: fp1-16|fp1+0.
	 */
	"*(u64 *)(r1 + 0) = r8;"
	/* read fp1-8: should be fp1-16|fp1+0 (joined) */
	"r0 = *(u64 *)(r10 - 8);"
	"*(u64 *)(r0 + 0) = 42;"
	"r0 = 0;"
	"exit;"
	:: __imm(bpf_get_prandom_u32)
	: __clobber_all);
}
