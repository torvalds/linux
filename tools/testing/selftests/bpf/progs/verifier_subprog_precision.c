// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2023 Meta Platforms, Inc. and affiliates. */

#include <errno.h>
#include <string.h>
#include <linux/bpf.h>
#include <bpf/bpf_helpers.h>
#include "bpf_misc.h"
#include <../../../tools/include/linux/filter.h>

int vals[] SEC(".data.vals") = {1, 2, 3, 4};

__naked __noinline __used
static unsigned long identity_subprog()
{
	/* the simplest *static* 64-bit identity function */
	asm volatile (
		"r0 = r1;"
		"exit;"
	);
}

__noinline __used
unsigned long global_identity_subprog(__u64 x)
{
	/* the simplest *global* 64-bit identity function */
	return x;
}

__naked __noinline __used
static unsigned long callback_subprog()
{
	/* the simplest callback function */
	asm volatile (
		"r0 = 0;"
		"exit;"
	);
}

SEC("?raw_tp")
__success __log_level(2)
__msg("7: (0f) r1 += r0")
__msg("mark_precise: frame0: regs=r0 stack= before 6: (bf) r1 = r7")
__msg("mark_precise: frame0: regs=r0 stack= before 5: (27) r0 *= 4")
__msg("mark_precise: frame0: regs=r0 stack= before 11: (95) exit")
__msg("mark_precise: frame1: regs=r0 stack= before 10: (bf) r0 = r1")
__msg("mark_precise: frame1: regs=r1 stack= before 4: (85) call pc+5")
__msg("mark_precise: frame0: regs=r1 stack= before 3: (bf) r1 = r6")
__msg("mark_precise: frame0: regs=r6 stack= before 2: (b7) r6 = 3")
__naked int subprog_result_precise(void)
{
	asm volatile (
		"r6 = 3;"
		/* pass r6 through r1 into subprog to get it back as r0;
		 * this whole chain will have to be marked as precise later
		 */
		"r1 = r6;"
		"call identity_subprog;"
		/* now use subprog's returned value (which is a
		 * r6 -> r1 -> r0 chain), as index into vals array, forcing
		 * all of that to be known precisely
		 */
		"r0 *= 4;"
		"r1 = %[vals];"
		/* here r0->r1->r6 chain is forced to be precise and has to be
		 * propagated back to the beginning, including through the
		 * subprog call
		 */
		"r1 += r0;"
		"r0 = *(u32 *)(r1 + 0);"
		"exit;"
		:
		: __imm_ptr(vals)
		: __clobber_common, "r6"
	);
}

__naked __noinline __used
static unsigned long fp_leaking_subprog()
{
	asm volatile (
		".8byte %[r0_eq_r10_cast_s8];"
		"exit;"
		:: __imm_insn(r0_eq_r10_cast_s8, BPF_MOVSX64_REG(BPF_REG_0, BPF_REG_10, 8))
	);
}

__naked __noinline __used
static unsigned long sneaky_fp_leaking_subprog()
{
	asm volatile (
		"r1 = r10;"
		".8byte %[r0_eq_r1_cast_s8];"
		"exit;"
		:: __imm_insn(r0_eq_r1_cast_s8, BPF_MOVSX64_REG(BPF_REG_0, BPF_REG_1, 8))
	);
}

SEC("?raw_tp")
__success __log_level(2)
__msg("6: (0f) r1 += r0")
__msg("mark_precise: frame0: last_idx 6 first_idx 0 subseq_idx -1")
__msg("mark_precise: frame0: regs=r0 stack= before 5: (bf) r1 = r6")
__msg("mark_precise: frame0: regs=r0 stack= before 4: (27) r0 *= 4")
__msg("mark_precise: frame0: regs=r0 stack= before 3: (57) r0 &= 3")
__msg("mark_precise: frame0: regs=r0 stack= before 10: (95) exit")
__msg("mark_precise: frame1: regs=r0 stack= before 9: (bf) r0 = (s8)r10")
__msg("7: R0=scalar")
__naked int fp_precise_subprog_result(void)
{
	asm volatile (
		"call fp_leaking_subprog;"
		/* use subprog's returned value (which is derived from r10=fp
		 * register), as index into vals array, forcing all of that to
		 * be known precisely
		 */
		"r0 &= 3;"
		"r0 *= 4;"
		"r1 = %[vals];"
		/* force precision marking */
		"r1 += r0;"
		"r0 = *(u32 *)(r1 + 0);"
		"exit;"
		:
		: __imm_ptr(vals)
		: __clobber_common
	);
}

SEC("?raw_tp")
__success __log_level(2)
__msg("6: (0f) r1 += r0")
__msg("mark_precise: frame0: last_idx 6 first_idx 0 subseq_idx -1")
__msg("mark_precise: frame0: regs=r0 stack= before 5: (bf) r1 = r6")
__msg("mark_precise: frame0: regs=r0 stack= before 4: (27) r0 *= 4")
__msg("mark_precise: frame0: regs=r0 stack= before 3: (57) r0 &= 3")
__msg("mark_precise: frame0: regs=r0 stack= before 11: (95) exit")
__msg("mark_precise: frame1: regs=r0 stack= before 10: (bf) r0 = (s8)r1")
/* here r1 is marked precise, even though it's fp register, but that's fine
 * because by the time we get out of subprogram it has to be derived from r10
 * anyways, at which point we'll break precision chain
 */
__msg("mark_precise: frame1: regs=r1 stack= before 9: (bf) r1 = r10")
__msg("7: R0=scalar")
__naked int sneaky_fp_precise_subprog_result(void)
{
	asm volatile (
		"call sneaky_fp_leaking_subprog;"
		/* use subprog's returned value (which is derived from r10=fp
		 * register), as index into vals array, forcing all of that to
		 * be known precisely
		 */
		"r0 &= 3;"
		"r0 *= 4;"
		"r1 = %[vals];"
		/* force precision marking */
		"r1 += r0;"
		"r0 = *(u32 *)(r1 + 0);"
		"exit;"
		:
		: __imm_ptr(vals)
		: __clobber_common
	);
}

SEC("?raw_tp")
__success __log_level(2)
__msg("9: (0f) r1 += r0")
__msg("mark_precise: frame0: last_idx 9 first_idx 0")
__msg("mark_precise: frame0: regs=r0 stack= before 8: (bf) r1 = r7")
__msg("mark_precise: frame0: regs=r0 stack= before 7: (27) r0 *= 4")
__msg("mark_precise: frame0: regs=r0 stack= before 5: (a5) if r0 < 0x4 goto pc+1")
__msg("mark_precise: frame0: regs=r0 stack= before 4: (85) call pc+7")
__naked int global_subprog_result_precise(void)
{
	asm volatile (
		"r6 = 3;"
		/* pass r6 through r1 into subprog to get it back as r0;
		 * given global_identity_subprog is global, precision won't
		 * propagate all the way back to r6
		 */
		"r1 = r6;"
		"call global_identity_subprog;"
		/* now use subprog's returned value (which is unknown now, so
		 * we need to clamp it), as index into vals array, forcing r0
		 * to be marked precise (with no effect on r6, though)
		 */
		"if r0 < %[vals_arr_sz] goto 1f;"
		"r0 = %[vals_arr_sz] - 1;"
	"1:"
		"r0 *= 4;"
		"r1 = %[vals];"
		/* here r0 is forced to be precise and has to be
		 * propagated back to the global subprog call, but it
		 * shouldn't go all the way to mark r6 as precise
		 */
		"r1 += r0;"
		"r0 = *(u32 *)(r1 + 0);"
		"exit;"
		:
		: __imm_ptr(vals),
		  __imm_const(vals_arr_sz, ARRAY_SIZE(vals))
		: __clobber_common, "r6"
	);
}

__naked __noinline __used
static unsigned long loop_callback_bad()
{
	/* bpf_loop() callback that can return values outside of [0, 1] range */
	asm volatile (
		"call %[bpf_get_prandom_u32];"
		"if r0 s> 1000 goto 1f;"
		"r0 = 0;"
	"1:"
		"goto +0;" /* checkpoint */
		/* bpf_loop() expects [0, 1] values, so branch above skipping
		 * r0 = 0; should lead to a failure, but if exit instruction
		 * doesn't enforce r0's precision, this callback will be
		 * successfully verified
		 */
		"exit;"
		:
		: __imm(bpf_get_prandom_u32)
		: __clobber_common
	);
}

SEC("?raw_tp")
__failure __log_level(2)
__flag(BPF_F_TEST_STATE_FREQ)
/* check that fallthrough code path marks r0 as precise */
__msg("mark_precise: frame1: regs=r0 stack= before 11: (b7) r0 = 0")
/* check that we have branch code path doing its own validation */
__msg("from 10 to 12: frame1: R0=scalar(smin=umin=1001")
/* check that branch code path marks r0 as precise, before failing */
__msg("mark_precise: frame1: regs=r0 stack= before 9: (85) call bpf_get_prandom_u32#7")
__msg("At callback return the register R0 has smin=1001 should have been in [0, 1]")
__naked int callback_precise_return_fail(void)
{
	asm volatile (
		"r1 = 1;"			/* nr_loops */
		"r2 = %[loop_callback_bad];"	/* callback_fn */
		"r3 = 0;"			/* callback_ctx */
		"r4 = 0;"			/* flags */
		"call %[bpf_loop];"

		"r0 = 0;"
		"exit;"
		:
		: __imm_ptr(loop_callback_bad),
		  __imm(bpf_loop)
		: __clobber_common
	);
}

SEC("?raw_tp")
__success __log_level(2)
/* First simulated path does not include callback body,
 * r1 and r4 are always precise for bpf_loop() calls.
 */
__msg("9: (85) call bpf_loop#181")
__msg("mark_precise: frame0: last_idx 9 first_idx 9 subseq_idx -1")
__msg("mark_precise: frame0: parent state regs=r4 stack=:")
__msg("mark_precise: frame0: last_idx 8 first_idx 0 subseq_idx 9")
__msg("mark_precise: frame0: regs=r4 stack= before 8: (b7) r4 = 0")
__msg("mark_precise: frame0: last_idx 9 first_idx 9 subseq_idx -1")
__msg("mark_precise: frame0: parent state regs=r1 stack=:")
__msg("mark_precise: frame0: last_idx 8 first_idx 0 subseq_idx 9")
__msg("mark_precise: frame0: regs=r1 stack= before 8: (b7) r4 = 0")
__msg("mark_precise: frame0: regs=r1 stack= before 7: (b7) r3 = 0")
__msg("mark_precise: frame0: regs=r1 stack= before 6: (bf) r2 = r8")
__msg("mark_precise: frame0: regs=r1 stack= before 5: (bf) r1 = r6")
__msg("mark_precise: frame0: regs=r6 stack= before 4: (b7) r6 = 3")
/* r6 precision propagation */
__msg("14: (0f) r1 += r6")
__msg("mark_precise: frame0: last_idx 14 first_idx 9")
__msg("mark_precise: frame0: regs=r6 stack= before 13: (bf) r1 = r7")
__msg("mark_precise: frame0: regs=r6 stack= before 12: (27) r6 *= 4")
__msg("mark_precise: frame0: regs=r6 stack= before 11: (25) if r6 > 0x3 goto pc+4")
__msg("mark_precise: frame0: regs=r0,r6 stack= before 10: (bf) r6 = r0")
__msg("mark_precise: frame0: regs=r0 stack= before 9: (85) call bpf_loop")
/* State entering callback body popped from states stack */
__msg("from 9 to 17: frame1:")
__msg("17: frame1: R1=scalar() R2=0 R10=fp0 cb")
__msg("17: (b7) r0 = 0")
__msg("18: (95) exit")
__msg("returning from callee:")
__msg("to caller at 9:")
__msg("frame 0: propagating r1,r4")
__msg("mark_precise: frame0: last_idx 9 first_idx 9 subseq_idx -1")
__msg("mark_precise: frame0: regs=r1,r4 stack= before 18: (95) exit")
__msg("from 18 to 9: safe")
__naked int callback_result_precise(void)
{
	asm volatile (
		"r6 = 3;"

		/* call subprog and use result; r0 shouldn't propagate back to
		 * callback_subprog
		 */
		"r1 = r6;"			/* nr_loops */
		"r2 = %[callback_subprog];"	/* callback_fn */
		"r3 = 0;"			/* callback_ctx */
		"r4 = 0;"			/* flags */
		"call %[bpf_loop];"

		"r6 = r0;"
		"if r6 > 3 goto 1f;"
		"r6 *= 4;"
		"r1 = %[vals];"
		/* here r6 is forced to be precise and has to be propagated
		 * back to the bpf_loop() call, but not beyond
		 */
		"r1 += r6;"
		"r0 = *(u32 *)(r1 + 0);"
	"1:"
		"exit;"
		:
		: __imm_ptr(vals),
		  __imm_ptr(callback_subprog),
		  __imm(bpf_loop)
		: __clobber_common, "r6"
	);
}

SEC("?raw_tp")
__success __log_level(2)
__msg("7: (0f) r1 += r6")
__msg("mark_precise: frame0: last_idx 7 first_idx 0")
__msg("mark_precise: frame0: regs=r6 stack= before 6: (bf) r1 = r7")
__msg("mark_precise: frame0: regs=r6 stack= before 5: (27) r6 *= 4")
__msg("mark_precise: frame0: regs=r6 stack= before 11: (95) exit")
__msg("mark_precise: frame1: regs= stack= before 10: (bf) r0 = r1")
__msg("mark_precise: frame1: regs= stack= before 4: (85) call pc+5")
__msg("mark_precise: frame0: regs=r6 stack= before 3: (b7) r1 = 0")
__msg("mark_precise: frame0: regs=r6 stack= before 2: (b7) r6 = 3")
__naked int parent_callee_saved_reg_precise(void)
{
	asm volatile (
		"r6 = 3;"

		/* call subprog and ignore result; we need this call only to
		 * complicate jump history
		 */
		"r1 = 0;"
		"call identity_subprog;"

		"r6 *= 4;"
		"r1 = %[vals];"
		/* here r6 is forced to be precise and has to be propagated
		 * back to the beginning, handling (and ignoring) subprog call
		 */
		"r1 += r6;"
		"r0 = *(u32 *)(r1 + 0);"
		"exit;"
		:
		: __imm_ptr(vals)
		: __clobber_common, "r6"
	);
}

SEC("?raw_tp")
__success __log_level(2)
__msg("7: (0f) r1 += r6")
__msg("mark_precise: frame0: last_idx 7 first_idx 0")
__msg("mark_precise: frame0: regs=r6 stack= before 6: (bf) r1 = r7")
__msg("mark_precise: frame0: regs=r6 stack= before 5: (27) r6 *= 4")
__msg("mark_precise: frame0: regs=r6 stack= before 4: (85) call pc+5")
__msg("mark_precise: frame0: regs=r6 stack= before 3: (b7) r1 = 0")
__msg("mark_precise: frame0: regs=r6 stack= before 2: (b7) r6 = 3")
__naked int parent_callee_saved_reg_precise_global(void)
{
	asm volatile (
		"r6 = 3;"

		/* call subprog and ignore result; we need this call only to
		 * complicate jump history
		 */
		"r1 = 0;"
		"call global_identity_subprog;"

		"r6 *= 4;"
		"r1 = %[vals];"
		/* here r6 is forced to be precise and has to be propagated
		 * back to the beginning, handling (and ignoring) subprog call
		 */
		"r1 += r6;"
		"r0 = *(u32 *)(r1 + 0);"
		"exit;"
		:
		: __imm_ptr(vals)
		: __clobber_common, "r6"
	);
}

SEC("?raw_tp")
__success __log_level(2)
/* First simulated path does not include callback body */
__msg("12: (0f) r1 += r6")
__msg("mark_precise: frame0: last_idx 12 first_idx 9")
__msg("mark_precise: frame0: regs=r6 stack= before 11: (bf) r1 = r7")
__msg("mark_precise: frame0: regs=r6 stack= before 10: (27) r6 *= 4")
__msg("mark_precise: frame0: regs=r6 stack= before 9: (85) call bpf_loop")
__msg("mark_precise: frame0: parent state regs=r6 stack=:")
__msg("mark_precise: frame0: last_idx 8 first_idx 0 subseq_idx 9")
__msg("mark_precise: frame0: regs=r6 stack= before 8: (b7) r4 = 0")
__msg("mark_precise: frame0: regs=r6 stack= before 7: (b7) r3 = 0")
__msg("mark_precise: frame0: regs=r6 stack= before 6: (bf) r2 = r8")
__msg("mark_precise: frame0: regs=r6 stack= before 5: (b7) r1 = 1")
__msg("mark_precise: frame0: regs=r6 stack= before 4: (b7) r6 = 3")
/* State entering callback body popped from states stack */
__msg("from 9 to 15: frame1:")
__msg("15: frame1: R1=scalar() R2=0 R10=fp0 cb")
__msg("15: (b7) r0 = 0")
__msg("16: (95) exit")
__msg("returning from callee:")
__msg("to caller at 9:")
/* r1, r4 are always precise for bpf_loop(),
 * r6 was marked before backtracking to callback body.
 */
__msg("frame 0: propagating r1,r4,r6")
__msg("mark_precise: frame0: last_idx 9 first_idx 9 subseq_idx -1")
__msg("mark_precise: frame0: regs=r1,r4,r6 stack= before 16: (95) exit")
__msg("mark_precise: frame1: regs= stack= before 15: (b7) r0 = 0")
__msg("mark_precise: frame1: regs= stack= before 9: (85) call bpf_loop")
__msg("mark_precise: frame0: parent state regs= stack=:")
__msg("from 16 to 9: safe")
__naked int parent_callee_saved_reg_precise_with_callback(void)
{
	asm volatile (
		"r6 = 3;"

		/* call subprog and ignore result; we need this call only to
		 * complicate jump history
		 */
		"r1 = 1;"			/* nr_loops */
		"r2 = %[callback_subprog];"	/* callback_fn */
		"r3 = 0;"			/* callback_ctx */
		"r4 = 0;"			/* flags */
		"call %[bpf_loop];"

		"r6 *= 4;"
		"r1 = %[vals];"
		/* here r6 is forced to be precise and has to be propagated
		 * back to the beginning, handling (and ignoring) callback call
		 */
		"r1 += r6;"
		"r0 = *(u32 *)(r1 + 0);"
		"exit;"
		:
		: __imm_ptr(vals),
		  __imm_ptr(callback_subprog),
		  __imm(bpf_loop)
		: __clobber_common, "r6"
	);
}

SEC("?raw_tp")
__success __log_level(2)
__msg("9: (0f) r1 += r6")
__msg("mark_precise: frame0: last_idx 9 first_idx 6")
__msg("mark_precise: frame0: regs=r6 stack= before 8: (bf) r1 = r7")
__msg("mark_precise: frame0: regs=r6 stack= before 7: (27) r6 *= 4")
__msg("mark_precise: frame0: regs=r6 stack= before 6: (79) r6 = *(u64 *)(r10 -8)")
__msg("mark_precise: frame0: parent state regs= stack=-8:")
__msg("mark_precise: frame0: last_idx 13 first_idx 0")
__msg("mark_precise: frame0: regs= stack=-8 before 13: (95) exit")
__msg("mark_precise: frame1: regs= stack= before 12: (bf) r0 = r1")
__msg("mark_precise: frame1: regs= stack= before 5: (85) call pc+6")
__msg("mark_precise: frame0: regs= stack=-8 before 4: (b7) r1 = 0")
__msg("mark_precise: frame0: regs= stack=-8 before 3: (7b) *(u64 *)(r10 -8) = r6")
__msg("mark_precise: frame0: regs=r6 stack= before 2: (b7) r6 = 3")
__naked int parent_stack_slot_precise(void)
{
	asm volatile (
		/* spill reg */
		"r6 = 3;"
		"*(u64 *)(r10 - 8) = r6;"

		/* call subprog and ignore result; we need this call only to
		 * complicate jump history
		 */
		"r1 = 0;"
		"call identity_subprog;"

		/* restore reg from stack; in this case we'll be carrying
		 * stack mask when going back into subprog through jump
		 * history
		 */
		"r6 = *(u64 *)(r10 - 8);"

		"r6 *= 4;"
		"r1 = %[vals];"
		/* here r6 is forced to be precise and has to be propagated
		 * back to the beginning, handling (and ignoring) subprog call
		 */
		"r1 += r6;"
		"r0 = *(u32 *)(r1 + 0);"
		"exit;"
		:
		: __imm_ptr(vals)
		: __clobber_common, "r6"
	);
}

SEC("?raw_tp")
__success __log_level(2)
__msg("9: (0f) r1 += r6")
__msg("mark_precise: frame0: last_idx 9 first_idx 0")
__msg("mark_precise: frame0: regs=r6 stack= before 8: (bf) r1 = r7")
__msg("mark_precise: frame0: regs=r6 stack= before 7: (27) r6 *= 4")
__msg("mark_precise: frame0: regs=r6 stack= before 6: (79) r6 = *(u64 *)(r10 -8)")
__msg("mark_precise: frame0: regs= stack=-8 before 5: (85) call pc+6")
__msg("mark_precise: frame0: regs= stack=-8 before 4: (b7) r1 = 0")
__msg("mark_precise: frame0: regs= stack=-8 before 3: (7b) *(u64 *)(r10 -8) = r6")
__msg("mark_precise: frame0: regs=r6 stack= before 2: (b7) r6 = 3")
__naked int parent_stack_slot_precise_global(void)
{
	asm volatile (
		/* spill reg */
		"r6 = 3;"
		"*(u64 *)(r10 - 8) = r6;"

		/* call subprog and ignore result; we need this call only to
		 * complicate jump history
		 */
		"r1 = 0;"
		"call global_identity_subprog;"

		/* restore reg from stack; in this case we'll be carrying
		 * stack mask when going back into subprog through jump
		 * history
		 */
		"r6 = *(u64 *)(r10 - 8);"

		"r6 *= 4;"
		"r1 = %[vals];"
		/* here r6 is forced to be precise and has to be propagated
		 * back to the beginning, handling (and ignoring) subprog call
		 */
		"r1 += r6;"
		"r0 = *(u32 *)(r1 + 0);"
		"exit;"
		:
		: __imm_ptr(vals)
		: __clobber_common, "r6"
	);
}

SEC("?raw_tp")
__success __log_level(2)
/* First simulated path does not include callback body */
__msg("14: (0f) r1 += r6")
__msg("mark_precise: frame0: last_idx 14 first_idx 10")
__msg("mark_precise: frame0: regs=r6 stack= before 13: (bf) r1 = r7")
__msg("mark_precise: frame0: regs=r6 stack= before 12: (27) r6 *= 4")
__msg("mark_precise: frame0: regs=r6 stack= before 11: (79) r6 = *(u64 *)(r10 -8)")
__msg("mark_precise: frame0: regs= stack=-8 before 10: (85) call bpf_loop")
__msg("mark_precise: frame0: parent state regs= stack=-8:")
__msg("mark_precise: frame0: last_idx 9 first_idx 0 subseq_idx 10")
__msg("mark_precise: frame0: regs= stack=-8 before 9: (b7) r4 = 0")
__msg("mark_precise: frame0: regs= stack=-8 before 8: (b7) r3 = 0")
__msg("mark_precise: frame0: regs= stack=-8 before 7: (bf) r2 = r8")
__msg("mark_precise: frame0: regs= stack=-8 before 6: (bf) r1 = r6")
__msg("mark_precise: frame0: regs= stack=-8 before 5: (7b) *(u64 *)(r10 -8) = r6")
__msg("mark_precise: frame0: regs=r6 stack= before 4: (b7) r6 = 3")
/* State entering callback body popped from states stack */
__msg("from 10 to 17: frame1:")
__msg("17: frame1: R1=scalar() R2=0 R10=fp0 cb")
__msg("17: (b7) r0 = 0")
__msg("18: (95) exit")
__msg("returning from callee:")
__msg("to caller at 10:")
/* r1, r4 are always precise for bpf_loop(),
 * fp-8 was marked before backtracking to callback body.
 */
__msg("frame 0: propagating r1,r4,fp-8")
__msg("mark_precise: frame0: last_idx 10 first_idx 10 subseq_idx -1")
__msg("mark_precise: frame0: regs=r1,r4 stack=-8 before 18: (95) exit")
__msg("mark_precise: frame1: regs= stack= before 17: (b7) r0 = 0")
__msg("mark_precise: frame1: regs= stack= before 10: (85) call bpf_loop#181")
__msg("mark_precise: frame0: parent state regs= stack=:")
__msg("from 18 to 10: safe")
__naked int parent_stack_slot_precise_with_callback(void)
{
	asm volatile (
		/* spill reg */
		"r6 = 3;"
		"*(u64 *)(r10 - 8) = r6;"

		/* ensure we have callback frame in jump history */
		"r1 = r6;"			/* nr_loops */
		"r2 = %[callback_subprog];"	/* callback_fn */
		"r3 = 0;"			/* callback_ctx */
		"r4 = 0;"			/* flags */
		"call %[bpf_loop];"

		/* restore reg from stack; in this case we'll be carrying
		 * stack mask when going back into subprog through jump
		 * history
		 */
		"r6 = *(u64 *)(r10 - 8);"

		"r6 *= 4;"
		"r1 = %[vals];"
		/* here r6 is forced to be precise and has to be propagated
		 * back to the beginning, handling (and ignoring) subprog call
		 */
		"r1 += r6;"
		"r0 = *(u32 *)(r1 + 0);"
		"exit;"
		:
		: __imm_ptr(vals),
		  __imm_ptr(callback_subprog),
		  __imm(bpf_loop)
		: __clobber_common, "r6"
	);
}

__noinline __used
static __u64 subprog_with_precise_arg(__u64 x)
{
	return vals[x]; /* x is forced to be precise */
}

SEC("?raw_tp")
__success __log_level(2)
__msg("8: (0f) r2 += r1")
__msg("mark_precise: frame1: last_idx 8 first_idx 0")
__msg("mark_precise: frame1: regs=r1 stack= before 6: (18) r2 = ")
__msg("mark_precise: frame1: regs=r1 stack= before 5: (67) r1 <<= 2")
__msg("mark_precise: frame1: regs=r1 stack= before 2: (85) call pc+2")
__msg("mark_precise: frame0: regs=r1 stack= before 1: (bf) r1 = r6")
__msg("mark_precise: frame0: regs=r6 stack= before 0: (b7) r6 = 3")
__naked int subprog_arg_precise(void)
{
	asm volatile (
		"r6 = 3;"
		"r1 = r6;"
		/* subprog_with_precise_arg expects its argument to be
		 * precise, so r1->r6 will be marked precise from inside the
		 * subprog
		 */
		"call subprog_with_precise_arg;"
		"r0 += r6;"
		"exit;"
		:
		:
		: __clobber_common, "r6"
	);
}

/* r1 is pointer to stack slot;
 * r2 is a register to spill into that slot
 * subprog also spills r2 into its own stack slot
 */
__naked __noinline __used
static __u64 subprog_spill_reg_precise(void)
{
	asm volatile (
		/* spill to parent stack */
		"*(u64 *)(r1 + 0) = r2;"
		/* spill to subprog stack (we use -16 offset to avoid
		 * accidental confusion with parent's -8 stack slot in
		 * verifier log output)
		 */
		"*(u64 *)(r10 - 16) = r2;"
		/* use both spills as return result to propagete precision everywhere */
		"r0 = *(u64 *)(r10 - 16);"
		"r2 = *(u64 *)(r1 + 0);"
		"r0 += r2;"
		"exit;"
	);
}

SEC("?raw_tp")
__success __log_level(2)
__msg("10: (0f) r1 += r7")
__msg("mark_precise: frame0: last_idx 10 first_idx 7 subseq_idx -1")
__msg("mark_precise: frame0: regs=r7 stack= before 9: (bf) r1 = r8")
__msg("mark_precise: frame0: regs=r7 stack= before 8: (27) r7 *= 4")
__msg("mark_precise: frame0: regs=r7 stack= before 7: (79) r7 = *(u64 *)(r10 -8)")
__msg("mark_precise: frame0: parent state regs= stack=-8:  R0=2 R6=1 R8=map_value(map=.data.vals,ks=4,vs=16) R10=fp0 fp-8=P1")
__msg("mark_precise: frame0: last_idx 18 first_idx 0 subseq_idx 7")
__msg("mark_precise: frame0: regs= stack=-8 before 18: (95) exit")
__msg("mark_precise: frame1: regs= stack= before 17: (0f) r0 += r2")
__msg("mark_precise: frame1: regs= stack= before 16: (79) r2 = *(u64 *)(r1 +0)")
__msg("mark_precise: frame1: regs= stack= before 15: (79) r0 = *(u64 *)(r10 -16)")
__msg("mark_precise: frame1: regs= stack= before 14: (7b) *(u64 *)(r10 -16) = r2")
__msg("mark_precise: frame1: regs= stack= before 13: (7b) *(u64 *)(r1 +0) = r2")
__msg("mark_precise: frame1: regs=r2 stack= before 6: (85) call pc+6")
__msg("mark_precise: frame0: regs=r2 stack= before 5: (bf) r2 = r6")
__msg("mark_precise: frame0: regs=r6 stack= before 4: (07) r1 += -8")
__msg("mark_precise: frame0: regs=r6 stack= before 3: (bf) r1 = r10")
__msg("mark_precise: frame0: regs=r6 stack= before 2: (b7) r6 = 1")
__naked int subprog_spill_into_parent_stack_slot_precise(void)
{
	asm volatile (
		"r6 = 1;"

		/* pass pointer to stack slot and r6 to subprog;
		 * r6 will be marked precise and spilled into fp-8 slot, which
		 * also should be marked precise
		 */
		"r1 = r10;"
		"r1 += -8;"
		"r2 = r6;"
		"call subprog_spill_reg_precise;"

		/* restore reg from stack; in this case we'll be carrying
		 * stack mask when going back into subprog through jump
		 * history
		 */
		"r7 = *(u64 *)(r10 - 8);"

		"r7 *= 4;"
		"r1 = %[vals];"
		/* here r7 is forced to be precise and has to be propagated
		 * back to the beginning, handling subprog call and logic
		 */
		"r1 += r7;"
		"r0 = *(u32 *)(r1 + 0);"
		"exit;"
		:
		: __imm_ptr(vals)
		: __clobber_common, "r6", "r7"
	);
}

SEC("?raw_tp")
__success __log_level(2)
__msg("17: (0f) r1 += r0")
__msg("mark_precise: frame0: last_idx 17 first_idx 0 subseq_idx -1")
__msg("mark_precise: frame0: regs=r0 stack= before 16: (bf) r1 = r7")
__msg("mark_precise: frame0: regs=r0 stack= before 15: (27) r0 *= 4")
__msg("mark_precise: frame0: regs=r0 stack= before 14: (79) r0 = *(u64 *)(r10 -16)")
__msg("mark_precise: frame0: regs= stack=-16 before 13: (7b) *(u64 *)(r7 -8) = r0")
__msg("mark_precise: frame0: regs=r0 stack= before 12: (79) r0 = *(u64 *)(r8 +16)")
__msg("mark_precise: frame0: regs= stack=-16 before 11: (7b) *(u64 *)(r8 +16) = r0")
__msg("mark_precise: frame0: regs=r0 stack= before 10: (79) r0 = *(u64 *)(r7 -8)")
__msg("mark_precise: frame0: regs= stack=-16 before 9: (7b) *(u64 *)(r10 -16) = r0")
__msg("mark_precise: frame0: regs=r0 stack= before 8: (07) r8 += -32")
__msg("mark_precise: frame0: regs=r0 stack= before 7: (bf) r8 = r10")
__msg("mark_precise: frame0: regs=r0 stack= before 6: (07) r7 += -8")
__msg("mark_precise: frame0: regs=r0 stack= before 5: (bf) r7 = r10")
__msg("mark_precise: frame0: regs=r0 stack= before 21: (95) exit")
__msg("mark_precise: frame1: regs=r0 stack= before 20: (bf) r0 = r1")
__msg("mark_precise: frame1: regs=r1 stack= before 4: (85) call pc+15")
__msg("mark_precise: frame0: regs=r1 stack= before 3: (bf) r1 = r6")
__msg("mark_precise: frame0: regs=r6 stack= before 2: (b7) r6 = 1")
__naked int stack_slot_aliases_precision(void)
{
	asm volatile (
		"r6 = 1;"
		/* pass r6 through r1 into subprog to get it back as r0;
		 * this whole chain will have to be marked as precise later
		 */
		"r1 = r6;"
		"call identity_subprog;"
		/* let's setup two registers that are aliased to r10 */
		"r7 = r10;"
		"r7 += -8;"			/* r7 = r10 - 8 */
		"r8 = r10;"
		"r8 += -32;"			/* r8 = r10 - 32 */
		/* now spill subprog's return value (a r6 -> r1 -> r0 chain)
		 * a few times through different stack pointer regs, making
		 * sure to use r10, r7, and r8 both in LDX and STX insns, and
		 * *importantly* also using a combination of const var_off and
		 * insn->off to validate that we record final stack slot
		 * correctly, instead of relying on just insn->off derivation,
		 * which is only valid for r10-based stack offset
		 */
		"*(u64 *)(r10 - 16) = r0;"
		"r0 = *(u64 *)(r7 - 8);"	/* r7 - 8 == r10 - 16 */
		"*(u64 *)(r8 + 16) = r0;"	/* r8 + 16 = r10 - 16 */
		"r0 = *(u64 *)(r8 + 16);"
		"*(u64 *)(r7 - 8) = r0;"
		"r0 = *(u64 *)(r10 - 16);"
		/* get ready to use r0 as an index into array to force precision */
		"r0 *= 4;"
		"r1 = %[vals];"
		/* here r0->r1->r6 chain is forced to be precise and has to be
		 * propagated back to the beginning, including through the
		 * subprog call and all the stack spills and loads
		 */
		"r1 += r0;"
		"r0 = *(u32 *)(r1 + 0);"
		"exit;"
		:
		: __imm_ptr(vals)
		: __clobber_common, "r6"
	);
}

char _license[] SEC("license") = "GPL";
