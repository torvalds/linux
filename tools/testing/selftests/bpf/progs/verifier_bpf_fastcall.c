// SPDX-License-Identifier: GPL-2.0

#include <linux/bpf.h>
#include <bpf/bpf_helpers.h>
#include <bpf/bpf_core_read.h>
#include "../../../include/linux/filter.h"
#include "bpf_misc.h"
#include <stdbool.h>
#include "bpf_kfuncs.h"

SEC("raw_tp")
__arch_x86_64
__log_level(4) __msg("stack depth 8")
__xlated("4: r5 = 5")
__xlated("5: w0 = ")
__xlated("6: r0 = &(void __percpu *)(r0)")
__xlated("7: r0 = *(u32 *)(r0 +0)")
__xlated("8: exit")
__success
__naked void simple(void)
{
	asm volatile (
	"r1 = 1;"
	"r2 = 2;"
	"r3 = 3;"
	"r4 = 4;"
	"r5 = 5;"
	"*(u64 *)(r10 - 16) = r1;"
	"*(u64 *)(r10 - 24) = r2;"
	"*(u64 *)(r10 - 32) = r3;"
	"*(u64 *)(r10 - 40) = r4;"
	"*(u64 *)(r10 - 48) = r5;"
	"call %[bpf_get_smp_processor_id];"
	"r5 = *(u64 *)(r10 - 48);"
	"r4 = *(u64 *)(r10 - 40);"
	"r3 = *(u64 *)(r10 - 32);"
	"r2 = *(u64 *)(r10 - 24);"
	"r1 = *(u64 *)(r10 - 16);"
	"exit;"
	:
	: __imm(bpf_get_smp_processor_id)
	: __clobber_all);
}

/* The logic for detecting and verifying bpf_fastcall pattern is the same for
 * any arch, however x86 differs from arm64 or riscv64 in a way
 * bpf_get_smp_processor_id is rewritten:
 * - on x86 it is done by verifier
 * - on arm64 and riscv64 it is done by jit
 *
 * Which leads to different xlated patterns for different archs:
 * - on x86 the call is expanded as 3 instructions
 * - on arm64 and riscv64 the call remains as is
 *   (but spills/fills are still removed)
 *
 * It is really desirable to check instruction indexes in the xlated
 * patterns, so add this canary test to check that function rewrite by
 * jit is correctly processed by bpf_fastcall logic, keep the rest of the
 * tests as x86.
 */
SEC("raw_tp")
__arch_arm64
__arch_riscv64
__xlated("0: r1 = 1")
__xlated("1: call bpf_get_smp_processor_id")
__xlated("2: exit")
__success
__naked void canary_arm64_riscv64(void)
{
	asm volatile (
	"r1 = 1;"
	"*(u64 *)(r10 - 16) = r1;"
	"call %[bpf_get_smp_processor_id];"
	"r1 = *(u64 *)(r10 - 16);"
	"exit;"
	:
	: __imm(bpf_get_smp_processor_id)
	: __clobber_all);
}

SEC("raw_tp")
__arch_x86_64
__xlated("1: r0 = &(void __percpu *)(r0)")
__xlated("...")
__xlated("3: exit")
__success
__naked void canary_zero_spills(void)
{
	asm volatile (
	"call %[bpf_get_smp_processor_id];"
	"exit;"
	:
	: __imm(bpf_get_smp_processor_id)
	: __clobber_all);
}

SEC("raw_tp")
__arch_x86_64
__log_level(4) __msg("stack depth 16")
__xlated("1: *(u64 *)(r10 -16) = r1")
__xlated("...")
__xlated("3: r0 = &(void __percpu *)(r0)")
__xlated("...")
__xlated("5: r2 = *(u64 *)(r10 -16)")
__success
__naked void wrong_reg_in_pattern1(void)
{
	asm volatile (
	"r1 = 1;"
	"*(u64 *)(r10 - 16) = r1;"
	"call %[bpf_get_smp_processor_id];"
	"r2 = *(u64 *)(r10 - 16);"
	"exit;"
	:
	: __imm(bpf_get_smp_processor_id)
	: __clobber_all);
}

SEC("raw_tp")
__arch_x86_64
__xlated("1: *(u64 *)(r10 -16) = r6")
__xlated("...")
__xlated("3: r0 = &(void __percpu *)(r0)")
__xlated("...")
__xlated("5: r6 = *(u64 *)(r10 -16)")
__success
__naked void wrong_reg_in_pattern2(void)
{
	asm volatile (
	"r6 = 1;"
	"*(u64 *)(r10 - 16) = r6;"
	"call %[bpf_get_smp_processor_id];"
	"r6 = *(u64 *)(r10 - 16);"
	"exit;"
	:
	: __imm(bpf_get_smp_processor_id)
	: __clobber_all);
}

SEC("raw_tp")
__arch_x86_64
__xlated("1: *(u64 *)(r10 -16) = r0")
__xlated("...")
__xlated("3: r0 = &(void __percpu *)(r0)")
__xlated("...")
__xlated("5: r0 = *(u64 *)(r10 -16)")
__success
__naked void wrong_reg_in_pattern3(void)
{
	asm volatile (
	"r0 = 1;"
	"*(u64 *)(r10 - 16) = r0;"
	"call %[bpf_get_smp_processor_id];"
	"r0 = *(u64 *)(r10 - 16);"
	"exit;"
	:
	: __imm(bpf_get_smp_processor_id)
	: __clobber_all);
}

SEC("raw_tp")
__arch_x86_64
__xlated("2: *(u64 *)(r2 -16) = r1")
__xlated("...")
__xlated("4: r0 = &(void __percpu *)(r0)")
__xlated("...")
__xlated("6: r1 = *(u64 *)(r10 -16)")
__success
__naked void wrong_base_in_pattern(void)
{
	asm volatile (
	"r1 = 1;"
	"r2 = r10;"
	"*(u64 *)(r2 - 16) = r1;"
	"call %[bpf_get_smp_processor_id];"
	"r1 = *(u64 *)(r10 - 16);"
	"exit;"
	:
	: __imm(bpf_get_smp_processor_id)
	: __clobber_all);
}

SEC("raw_tp")
__arch_x86_64
__xlated("1: *(u64 *)(r10 -16) = r1")
__xlated("...")
__xlated("3: r0 = &(void __percpu *)(r0)")
__xlated("...")
__xlated("5: r2 = 1")
__success
__naked void wrong_insn_in_pattern(void)
{
	asm volatile (
	"r1 = 1;"
	"*(u64 *)(r10 - 16) = r1;"
	"call %[bpf_get_smp_processor_id];"
	"r2 = 1;"
	"r1 = *(u64 *)(r10 - 16);"
	"exit;"
	:
	: __imm(bpf_get_smp_processor_id)
	: __clobber_all);
}

SEC("raw_tp")
__arch_x86_64
__xlated("2: *(u64 *)(r10 -16) = r1")
__xlated("...")
__xlated("4: r0 = &(void __percpu *)(r0)")
__xlated("...")
__xlated("6: r1 = *(u64 *)(r10 -8)")
__success
__naked void wrong_off_in_pattern1(void)
{
	asm volatile (
	"r1 = 1;"
	"*(u64 *)(r10 - 8) = r1;"
	"*(u64 *)(r10 - 16) = r1;"
	"call %[bpf_get_smp_processor_id];"
	"r1 = *(u64 *)(r10 - 8);"
	"exit;"
	:
	: __imm(bpf_get_smp_processor_id)
	: __clobber_all);
}

SEC("raw_tp")
__arch_x86_64
__xlated("1: *(u32 *)(r10 -4) = r1")
__xlated("...")
__xlated("3: r0 = &(void __percpu *)(r0)")
__xlated("...")
__xlated("5: r1 = *(u32 *)(r10 -4)")
__success
__naked void wrong_off_in_pattern2(void)
{
	asm volatile (
	"r1 = 1;"
	"*(u32 *)(r10 - 4) = r1;"
	"call %[bpf_get_smp_processor_id];"
	"r1 = *(u32 *)(r10 - 4);"
	"exit;"
	:
	: __imm(bpf_get_smp_processor_id)
	: __clobber_all);
}

SEC("raw_tp")
__arch_x86_64
__xlated("1: *(u32 *)(r10 -16) = r1")
__xlated("...")
__xlated("3: r0 = &(void __percpu *)(r0)")
__xlated("...")
__xlated("5: r1 = *(u32 *)(r10 -16)")
__success
__naked void wrong_size_in_pattern(void)
{
	asm volatile (
	"r1 = 1;"
	"*(u32 *)(r10 - 16) = r1;"
	"call %[bpf_get_smp_processor_id];"
	"r1 = *(u32 *)(r10 - 16);"
	"exit;"
	:
	: __imm(bpf_get_smp_processor_id)
	: __clobber_all);
}

SEC("raw_tp")
__arch_x86_64
__xlated("2: *(u32 *)(r10 -8) = r1")
__xlated("...")
__xlated("4: r0 = &(void __percpu *)(r0)")
__xlated("...")
__xlated("6: r1 = *(u32 *)(r10 -8)")
__success
__naked void partial_pattern(void)
{
	asm volatile (
	"r1 = 1;"
	"r2 = 2;"
	"*(u32 *)(r10 - 8) = r1;"
	"*(u64 *)(r10 - 16) = r2;"
	"call %[bpf_get_smp_processor_id];"
	"r2 = *(u64 *)(r10 - 16);"
	"r1 = *(u32 *)(r10 - 8);"
	"exit;"
	:
	: __imm(bpf_get_smp_processor_id)
	: __clobber_all);
}

SEC("raw_tp")
__arch_x86_64
__xlated("0: r1 = 1")
__xlated("1: r2 = 2")
/* not patched, spills for -8, -16 not removed */
__xlated("2: *(u64 *)(r10 -8) = r1")
__xlated("3: *(u64 *)(r10 -16) = r2")
__xlated("...")
__xlated("5: r0 = &(void __percpu *)(r0)")
__xlated("...")
__xlated("7: r2 = *(u64 *)(r10 -16)")
__xlated("8: r1 = *(u64 *)(r10 -8)")
/* patched, spills for -24, -32 removed */
__xlated("...")
__xlated("10: r0 = &(void __percpu *)(r0)")
__xlated("...")
__xlated("12: exit")
__success
__naked void min_stack_offset(void)
{
	asm volatile (
	"r1 = 1;"
	"r2 = 2;"
	/* this call won't be patched */
	"*(u64 *)(r10 - 8) = r1;"
	"*(u64 *)(r10 - 16) = r2;"
	"call %[bpf_get_smp_processor_id];"
	"r2 = *(u64 *)(r10 - 16);"
	"r1 = *(u64 *)(r10 - 8);"
	/* this call would be patched */
	"*(u64 *)(r10 - 24) = r1;"
	"*(u64 *)(r10 - 32) = r2;"
	"call %[bpf_get_smp_processor_id];"
	"r2 = *(u64 *)(r10 - 32);"
	"r1 = *(u64 *)(r10 - 24);"
	"exit;"
	:
	: __imm(bpf_get_smp_processor_id)
	: __clobber_all);
}

SEC("raw_tp")
__arch_x86_64
__xlated("1: *(u64 *)(r10 -8) = r1")
__xlated("...")
__xlated("3: r0 = &(void __percpu *)(r0)")
__xlated("...")
__xlated("5: r1 = *(u64 *)(r10 -8)")
__success
__naked void bad_fixed_read(void)
{
	asm volatile (
	"r1 = 1;"
	"*(u64 *)(r10 - 8) = r1;"
	"call %[bpf_get_smp_processor_id];"
	"r1 = *(u64 *)(r10 - 8);"
	"r1 = r10;"
	"r1 += -8;"
	"r1 = *(u64 *)(r1 - 0);"
	"exit;"
	:
	: __imm(bpf_get_smp_processor_id)
	: __clobber_all);
}

SEC("raw_tp")
__arch_x86_64
__xlated("1: *(u64 *)(r10 -8) = r1")
__xlated("...")
__xlated("3: r0 = &(void __percpu *)(r0)")
__xlated("...")
__xlated("5: r1 = *(u64 *)(r10 -8)")
__success
__naked void bad_fixed_write(void)
{
	asm volatile (
	"r1 = 1;"
	"*(u64 *)(r10 - 8) = r1;"
	"call %[bpf_get_smp_processor_id];"
	"r1 = *(u64 *)(r10 - 8);"
	"r1 = r10;"
	"r1 += -8;"
	"*(u64 *)(r1 - 0) = r1;"
	"exit;"
	:
	: __imm(bpf_get_smp_processor_id)
	: __clobber_all);
}

SEC("raw_tp")
__arch_x86_64
__xlated("6: *(u64 *)(r10 -16) = r1")
__xlated("...")
__xlated("8: r0 = &(void __percpu *)(r0)")
__xlated("...")
__xlated("10: r1 = *(u64 *)(r10 -16)")
__success
__naked void bad_varying_read(void)
{
	asm volatile (
	"r6 = *(u64 *)(r1 + 0);" /* random scalar value */
	"r6 &= 0x7;"		 /* r6 range [0..7] */
	"r6 += 0x2;"		 /* r6 range [2..9] */
	"r7 = 0;"
	"r7 -= r6;"		 /* r7 range [-9..-2] */
	"r1 = 1;"
	"*(u64 *)(r10 - 16) = r1;"
	"call %[bpf_get_smp_processor_id];"
	"r1 = *(u64 *)(r10 - 16);"
	"r1 = r10;"
	"r1 += r7;"
	"r1 = *(u8 *)(r1 - 0);" /* touches slot [-16..-9] where spills are stored */
	"exit;"
	:
	: __imm(bpf_get_smp_processor_id)
	: __clobber_all);
}

SEC("raw_tp")
__arch_x86_64
__xlated("6: *(u64 *)(r10 -16) = r1")
__xlated("...")
__xlated("8: r0 = &(void __percpu *)(r0)")
__xlated("...")
__xlated("10: r1 = *(u64 *)(r10 -16)")
__success
__naked void bad_varying_write(void)
{
	asm volatile (
	"r6 = *(u64 *)(r1 + 0);" /* random scalar value */
	"r6 &= 0x7;"		 /* r6 range [0..7] */
	"r6 += 0x2;"		 /* r6 range [2..9] */
	"r7 = 0;"
	"r7 -= r6;"		 /* r7 range [-9..-2] */
	"r1 = 1;"
	"*(u64 *)(r10 - 16) = r1;"
	"call %[bpf_get_smp_processor_id];"
	"r1 = *(u64 *)(r10 - 16);"
	"r1 = r10;"
	"r1 += r7;"
	"*(u8 *)(r1 - 0) = r7;" /* touches slot [-16..-9] where spills are stored */
	"exit;"
	:
	: __imm(bpf_get_smp_processor_id)
	: __clobber_all);
}

SEC("raw_tp")
__arch_x86_64
__xlated("1: *(u64 *)(r10 -8) = r1")
__xlated("...")
__xlated("3: r0 = &(void __percpu *)(r0)")
__xlated("...")
__xlated("5: r1 = *(u64 *)(r10 -8)")
__success
__naked void bad_write_in_subprog(void)
{
	asm volatile (
	"r1 = 1;"
	"*(u64 *)(r10 - 8) = r1;"
	"call %[bpf_get_smp_processor_id];"
	"r1 = *(u64 *)(r10 - 8);"
	"r1 = r10;"
	"r1 += -8;"
	"call bad_write_in_subprog_aux;"
	"exit;"
	:
	: __imm(bpf_get_smp_processor_id)
	: __clobber_all);
}

__used
__naked static void bad_write_in_subprog_aux(void)
{
	asm volatile (
	"r0 = 1;"
	"*(u64 *)(r1 - 0) = r0;"	/* invalidates bpf_fastcall contract for caller: */
	"exit;"				/* caller stack at -8 used outside of the pattern */
	::: __clobber_all);
}

SEC("raw_tp")
__arch_x86_64
__xlated("1: *(u64 *)(r10 -8) = r1")
__xlated("...")
__xlated("3: r0 = &(void __percpu *)(r0)")
__xlated("...")
__xlated("5: r1 = *(u64 *)(r10 -8)")
__success
__naked void bad_helper_write(void)
{
	asm volatile (
	"r1 = 1;"
	/* bpf_fastcall pattern with stack offset -8 */
	"*(u64 *)(r10 - 8) = r1;"
	"call %[bpf_get_smp_processor_id];"
	"r1 = *(u64 *)(r10 - 8);"
	"r1 = r10;"
	"r1 += -8;"
	"r2 = 1;"
	"r3 = 42;"
	/* read dst is fp[-8], thus bpf_fastcall rewrite not applied */
	"call %[bpf_probe_read_kernel];"
	"exit;"
	:
	: __imm(bpf_get_smp_processor_id),
	  __imm(bpf_probe_read_kernel)
	: __clobber_all);
}

SEC("raw_tp")
__arch_x86_64
/* main, not patched */
__xlated("1: *(u64 *)(r10 -8) = r1")
__xlated("...")
__xlated("3: r0 = &(void __percpu *)(r0)")
__xlated("...")
__xlated("5: r1 = *(u64 *)(r10 -8)")
__xlated("...")
__xlated("9: call pc+1")
__xlated("...")
__xlated("10: exit")
/* subprogram, patched */
__xlated("11: r1 = 1")
__xlated("...")
__xlated("13: r0 = &(void __percpu *)(r0)")
__xlated("...")
__xlated("15: exit")
__success
__naked void invalidate_one_subprog(void)
{
	asm volatile (
	"r1 = 1;"
	"*(u64 *)(r10 - 8) = r1;"
	"call %[bpf_get_smp_processor_id];"
	"r1 = *(u64 *)(r10 - 8);"
	"r1 = r10;"
	"r1 += -8;"
	"r1 = *(u64 *)(r1 - 0);"
	"call invalidate_one_subprog_aux;"
	"exit;"
	:
	: __imm(bpf_get_smp_processor_id)
	: __clobber_all);
}

__used
__naked static void invalidate_one_subprog_aux(void)
{
	asm volatile (
	"r1 = 1;"
	"*(u64 *)(r10 - 8) = r1;"
	"call %[bpf_get_smp_processor_id];"
	"r1 = *(u64 *)(r10 - 8);"
	"exit;"
	:
	: __imm(bpf_get_smp_processor_id)
	: __clobber_all);
}

SEC("raw_tp")
__arch_x86_64
/* main */
__xlated("0: r1 = 1")
__xlated("...")
__xlated("2: r0 = &(void __percpu *)(r0)")
__xlated("...")
__xlated("4: call pc+1")
__xlated("5: exit")
/* subprogram */
__xlated("6: r1 = 1")
__xlated("...")
__xlated("8: r0 = &(void __percpu *)(r0)")
__xlated("...")
__xlated("10: *(u64 *)(r10 -16) = r1")
__xlated("11: exit")
__success
__naked void subprogs_use_independent_offsets(void)
{
	asm volatile (
	"r1 = 1;"
	"*(u64 *)(r10 - 16) = r1;"
	"call %[bpf_get_smp_processor_id];"
	"r1 = *(u64 *)(r10 - 16);"
	"call subprogs_use_independent_offsets_aux;"
	"exit;"
	:
	: __imm(bpf_get_smp_processor_id)
	: __clobber_all);
}

__used
__naked static void subprogs_use_independent_offsets_aux(void)
{
	asm volatile (
	"r1 = 1;"
	"*(u64 *)(r10 - 24) = r1;"
	"call %[bpf_get_smp_processor_id];"
	"r1 = *(u64 *)(r10 - 24);"
	"*(u64 *)(r10 - 16) = r1;"
	"exit;"
	:
	: __imm(bpf_get_smp_processor_id)
	: __clobber_all);
}

SEC("raw_tp")
__arch_x86_64
__log_level(4) __msg("stack depth 8")
__xlated("2: r0 = &(void __percpu *)(r0)")
__success
__naked void helper_call_does_not_prevent_bpf_fastcall(void)
{
	asm volatile (
	"r1 = 1;"
	"*(u64 *)(r10 - 8) = r1;"
	"call %[bpf_get_smp_processor_id];"
	"r1 = *(u64 *)(r10 - 8);"
	"*(u64 *)(r10 - 8) = r1;"
	"call %[bpf_get_prandom_u32];"
	"r1 = *(u64 *)(r10 - 8);"
	"exit;"
	:
	: __imm(bpf_get_smp_processor_id),
	  __imm(bpf_get_prandom_u32)
	: __clobber_all);
}

SEC("raw_tp")
__arch_x86_64
__log_level(4) __msg("stack depth 24")
/* may_goto counter at -24 */
__xlated("0: *(u64 *)(r10 -24) =")
/* may_goto timestamp at -16 */
__xlated("1: *(u64 *)(r10 -16) =")
__xlated("2: r1 = 1")
__xlated("...")
__xlated("4: r0 = &(void __percpu *)(r0)")
__xlated("...")
/* may_goto expansion starts */
__xlated("6: r11 = *(u64 *)(r10 -24)")
__xlated("7: if r11 == 0x0 goto pc+6")
__xlated("8: r11 -= 1")
__xlated("9: if r11 != 0x0 goto pc+2")
__xlated("10: r11 = -24")
__xlated("11: call unknown")
__xlated("12: *(u64 *)(r10 -24) = r11")
/* may_goto expansion ends */
__xlated("13: *(u64 *)(r10 -8) = r1")
__xlated("14: exit")
__success
__naked void may_goto_interaction_x86_64(void)
{
	asm volatile (
	"r1 = 1;"
	"*(u64 *)(r10 - 16) = r1;"
	"call %[bpf_get_smp_processor_id];"
	"r1 = *(u64 *)(r10 - 16);"
	".8byte %[may_goto];"
	/* just touch some stack at -8 */
	"*(u64 *)(r10 - 8) = r1;"
	"exit;"
	:
	: __imm(bpf_get_smp_processor_id),
	  __imm_insn(may_goto, BPF_RAW_INSN(BPF_JMP | BPF_JCOND, 0, 0, +1 /* offset */, 0))
	: __clobber_all);
}

SEC("raw_tp")
__arch_arm64
__log_level(4) __msg("stack depth 16")
/* may_goto counter at -16 */
__xlated("0: *(u64 *)(r10 -16) =")
__xlated("1: r1 = 1")
__xlated("2: call bpf_get_smp_processor_id")
/* may_goto expansion starts */
__xlated("3: r11 = *(u64 *)(r10 -16)")
__xlated("4: if r11 == 0x0 goto pc+3")
__xlated("5: r11 -= 1")
__xlated("6: *(u64 *)(r10 -16) = r11")
/* may_goto expansion ends */
__xlated("7: *(u64 *)(r10 -8) = r1")
__xlated("8: exit")
__success
__naked void may_goto_interaction_arm64(void)
{
	asm volatile (
	"r1 = 1;"
	"*(u64 *)(r10 - 16) = r1;"
	"call %[bpf_get_smp_processor_id];"
	"r1 = *(u64 *)(r10 - 16);"
	".8byte %[may_goto];"
	/* just touch some stack at -8 */
	"*(u64 *)(r10 - 8) = r1;"
	"exit;"
	:
	: __imm(bpf_get_smp_processor_id),
	  __imm_insn(may_goto, BPF_RAW_INSN(BPF_JMP | BPF_JCOND, 0, 0, +1 /* offset */, 0))
	: __clobber_all);
}

__used
__naked static void dummy_loop_callback(void)
{
	asm volatile (
	"r0 = 0;"
	"exit;"
	::: __clobber_all);
}

SEC("raw_tp")
__arch_x86_64
__log_level(4) __msg("stack depth 32+0")
__xlated("2: r1 = 1")
__xlated("3: w0 =")
__xlated("4: r0 = &(void __percpu *)(r0)")
__xlated("5: r0 = *(u32 *)(r0 +0)")
/* bpf_loop params setup */
__xlated("6: r2 =")
__xlated("7: r3 = 0")
__xlated("8: r4 = 0")
__xlated("...")
/* ... part of the inlined bpf_loop */
__xlated("12: *(u64 *)(r10 -32) = r6")
__xlated("13: *(u64 *)(r10 -24) = r7")
__xlated("14: *(u64 *)(r10 -16) = r8")
__xlated("...")
__xlated("21: call pc+8") /* dummy_loop_callback */
/* ... last insns of the bpf_loop_interaction1 */
__xlated("...")
__xlated("28: r0 = 0")
__xlated("29: exit")
/* dummy_loop_callback */
__xlated("30: r0 = 0")
__xlated("31: exit")
__success
__naked int bpf_loop_interaction1(void)
{
	asm volatile (
	"r1 = 1;"
	/* bpf_fastcall stack region at -16, but could be removed */
	"*(u64 *)(r10 - 16) = r1;"
	"call %[bpf_get_smp_processor_id];"
	"r1 = *(u64 *)(r10 - 16);"
	"r2 = %[dummy_loop_callback];"
	"r3 = 0;"
	"r4 = 0;"
	"call %[bpf_loop];"
	"r0 = 0;"
	"exit;"
	:
	: __imm_ptr(dummy_loop_callback),
	  __imm(bpf_get_smp_processor_id),
	  __imm(bpf_loop)
	: __clobber_common
	);
}

SEC("raw_tp")
__arch_x86_64
__log_level(4) __msg("stack depth 40+0")
/* call bpf_get_smp_processor_id */
__xlated("2: r1 = 42")
__xlated("3: w0 =")
__xlated("4: r0 = &(void __percpu *)(r0)")
__xlated("5: r0 = *(u32 *)(r0 +0)")
/* call bpf_get_prandom_u32 */
__xlated("6: *(u64 *)(r10 -16) = r1")
__xlated("7: call")
__xlated("8: r1 = *(u64 *)(r10 -16)")
__xlated("...")
/* ... part of the inlined bpf_loop */
__xlated("15: *(u64 *)(r10 -40) = r6")
__xlated("16: *(u64 *)(r10 -32) = r7")
__xlated("17: *(u64 *)(r10 -24) = r8")
__success
__naked int bpf_loop_interaction2(void)
{
	asm volatile (
	"r1 = 42;"
	/* bpf_fastcall stack region at -16, cannot be removed */
	"*(u64 *)(r10 - 16) = r1;"
	"call %[bpf_get_smp_processor_id];"
	"r1 = *(u64 *)(r10 - 16);"
	"*(u64 *)(r10 - 16) = r1;"
	"call %[bpf_get_prandom_u32];"
	"r1 = *(u64 *)(r10 - 16);"
	"r2 = %[dummy_loop_callback];"
	"r3 = 0;"
	"r4 = 0;"
	"call %[bpf_loop];"
	"r0 = 0;"
	"exit;"
	:
	: __imm_ptr(dummy_loop_callback),
	  __imm(bpf_get_smp_processor_id),
	  __imm(bpf_get_prandom_u32),
	  __imm(bpf_loop)
	: __clobber_common
	);
}

SEC("raw_tp")
__arch_x86_64
__log_level(4)
__msg("stack depth 512+0")
/* just to print xlated version when debugging */
__xlated("r0 = &(void __percpu *)(r0)")
__success
/* cumulative_stack_depth() stack usage is MAX_BPF_STACK,
 * called subprogram uses an additional slot for bpf_fastcall spill/fill,
 * since bpf_fastcall spill/fill could be removed the program still fits
 * in MAX_BPF_STACK and should be accepted.
 */
__naked int cumulative_stack_depth(void)
{
	asm volatile(
	"r1 = 42;"
	"*(u64 *)(r10 - %[max_bpf_stack]) = r1;"
	"call cumulative_stack_depth_subprog;"
	"exit;"
	:
	: __imm_const(max_bpf_stack, MAX_BPF_STACK)
	: __clobber_all
	);
}

__used
__naked static void cumulative_stack_depth_subprog(void)
{
	asm volatile (
	"*(u64 *)(r10 - 8) = r1;"
	"call %[bpf_get_smp_processor_id];"
	"r1 = *(u64 *)(r10 - 8);"
	"exit;"
	:: __imm(bpf_get_smp_processor_id) : __clobber_all);
}

SEC("cgroup/getsockname_unix")
__xlated("0: r2 = 1")
/* bpf_cast_to_kern_ctx is replaced by a single assignment */
__xlated("1: r0 = r1")
__xlated("2: r0 = r2")
__xlated("3: exit")
__success
__naked void kfunc_bpf_cast_to_kern_ctx(void)
{
	asm volatile (
	"r2 = 1;"
	"*(u64 *)(r10 - 32) = r2;"
	"call %[bpf_cast_to_kern_ctx];"
	"r2 = *(u64 *)(r10 - 32);"
	"r0 = r2;"
	"exit;"
	:
	: __imm(bpf_cast_to_kern_ctx)
	: __clobber_all);
}

SEC("raw_tp")
__xlated("3: r3 = 1")
/* bpf_rdonly_cast is replaced by a single assignment */
__xlated("4: r0 = r1")
__xlated("5: r0 = r3")
void kfunc_bpf_rdonly_cast(void)
{
	asm volatile (
	"r2 = %[btf_id];"
	"r3 = 1;"
	"*(u64 *)(r10 - 32) = r3;"
	"call %[bpf_rdonly_cast];"
	"r3 = *(u64 *)(r10 - 32);"
	"r0 = r3;"
	:
	: __imm(bpf_rdonly_cast),
	 [btf_id]"r"(bpf_core_type_id_kernel(union bpf_attr))
	: __clobber_common);
}

/* BTF FUNC records are not generated for kfuncs referenced
 * from inline assembly. These records are necessary for
 * libbpf to link the program. The function below is a hack
 * to ensure that BTF FUNC records are generated.
 */
void kfunc_root(void)
{
	bpf_cast_to_kern_ctx(0);
	bpf_rdonly_cast(0, 0);
}

char _license[] SEC("license") = "GPL";
