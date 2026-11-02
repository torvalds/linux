// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2025 Meta Platforms, Inc. and affiliates. */

#include <linux/bpf.h>
#include <bpf/bpf_helpers.h>
#include "../../../include/linux/filter.h"
#include "bpf_misc.h"

SEC("raw_tp")
__description("may_goto 0")
__arch_x86_64
__arch_s390x
__arch_arm64
__arch_riscv64
__arch_loongarch
__xlated("0: r0 = 1")
__xlated("1: exit")
__success
__naked void may_goto_simple(void)
{
	asm volatile (
	".8byte %[may_goto];"
	"r0 = 1;"
	".8byte %[may_goto];"
	"exit;"
	:
	: __imm_insn(may_goto, BPF_RAW_INSN(BPF_JMP | BPF_JCOND, 0, 0, 0 /* offset */, 0))
	: __clobber_all);
}

SEC("raw_tp")
__description("batch 2 of may_goto 0")
__arch_x86_64
__arch_s390x
__arch_arm64
__arch_riscv64
__arch_loongarch
__xlated("0: r0 = 1")
__xlated("1: exit")
__success
__naked void may_goto_batch_0(void)
{
	asm volatile (
	".8byte %[may_goto1];"
	".8byte %[may_goto1];"
	"r0 = 1;"
	".8byte %[may_goto1];"
	".8byte %[may_goto1];"
	"exit;"
	:
	: __imm_insn(may_goto1, BPF_RAW_INSN(BPF_JMP | BPF_JCOND, 0, 0, 0 /* offset */, 0))
	: __clobber_all);
}

SEC("raw_tp")
__description("may_goto batch with offsets 2/1/0")
__arch_x86_64
__arch_s390x
__arch_arm64
__arch_riscv64
__arch_loongarch
__xlated("0: r0 = 1")
__xlated("1: exit")
__success
__naked void may_goto_batch_1(void)
{
	asm volatile (
	".8byte %[may_goto1];"
	".8byte %[may_goto2];"
	".8byte %[may_goto3];"
	"r0 = 1;"
	".8byte %[may_goto1];"
	".8byte %[may_goto2];"
	".8byte %[may_goto3];"
	"exit;"
	:
	: __imm_insn(may_goto1, BPF_RAW_INSN(BPF_JMP | BPF_JCOND, 0, 0, 2 /* offset */, 0)),
	  __imm_insn(may_goto2, BPF_RAW_INSN(BPF_JMP | BPF_JCOND, 0, 0, 1 /* offset */, 0)),
	  __imm_insn(may_goto3, BPF_RAW_INSN(BPF_JMP | BPF_JCOND, 0, 0, 0 /* offset */, 0))
	: __clobber_all);
}

SEC("raw_tp")
__description("may_goto batch with offsets 2/0")
__arch_x86_64
__arch_s390x
__arch_arm64
__arch_riscv64
__arch_loongarch
__xlated("0: *(u64 *)(r10 -16) = 65535")
__xlated("1: *(u64 *)(r10 -8) = 0")
__xlated("2: r12 = *(u64 *)(r10 -16)")
__xlated("3: if r12 == 0x0 goto pc+6")
__xlated("4: r12 -= 1")
__xlated("5: if r12 != 0x0 goto pc+2")
__xlated("6: r12 = -16")
__xlated("7: call unknown")
__xlated("8: *(u64 *)(r10 -16) = r12")
__xlated("9: r0 = 1")
__xlated("10: r0 = 2")
__xlated("11: exit")
__success
__naked void may_goto_batch_2(void)
{
	asm volatile (
	".8byte %[may_goto1];"
	".8byte %[may_goto3];"
	"r0 = 1;"
	"r0 = 2;"
	"exit;"
	:
	: __imm_insn(may_goto1, BPF_RAW_INSN(BPF_JMP | BPF_JCOND, 0, 0, 2 /* offset */, 0)),
	  __imm_insn(may_goto3, BPF_RAW_INSN(BPF_JMP | BPF_JCOND, 0, 0, 0 /* offset */, 0))
	: __clobber_all);
}

/*
 * Use bpf_get_prandom_u32() to prevent DCE from removing the checks.
 * retval: 0=all ok, 1-6=R0-R5 clobbered.
 */
SEC("syscall")
__description("timed may_goto preserves R0-R5")
__arch_x86_64
__arch_s390x
__arch_arm64
__arch_riscv64
__arch_loongarch
__success
__retval(0)
__naked void timed_may_goto_preserves_regs(void)
{
	asm volatile (
	"call %[bpf_get_prandom_u32];"
	"r6 = r0;"
	"r0 = 0x1111;"
	"r0 += r6;"
	"r1 = 0x2222;"
	"r1 += r6;"
	"r2 = 0x3333;"
	"r2 += r6;"
	"r3 = 0x4444;"
	"r3 += r6;"
	"r4 = 0x5555;"
	"r4 += r6;"
	"r5 = 0x6666;"
	"r5 += r6;"
	".8byte %[may_goto];"
	".8byte %[loop];"
	"r0 -= r6;"
	"r1 -= r6;"
	"r2 -= r6;"
	"r3 -= r6;"
	"r4 -= r6;"
	"r5 -= r6;"
	"if r0 != 0x1111 goto 1f;"
	"if r1 != 0x2222 goto 2f;"
	"if r2 != 0x3333 goto 3f;"
	"if r3 != 0x4444 goto 4f;"
	"if r4 != 0x5555 goto 5f;"
	"if r5 != 0x6666 goto 6f;"
	"r0 = 0;"
	"exit;"
	"1: r0 = 1; exit;"
	"2: r0 = 2; exit;"
	"3: r0 = 3; exit;"
	"4: r0 = 4; exit;"
	"5: r0 = 5; exit;"
	"6: r0 = 6; exit;"
	:
	: __imm(bpf_get_prandom_u32),
	  __imm_insn(may_goto, BPF_RAW_INSN(BPF_JMP | BPF_JCOND, 0, 0, 1, 0)),
	  __imm_insn(loop, BPF_RAW_INSN(BPF_JMP | BPF_JA, 0, 0, -2, 0))
	: __clobber_all);
}

__used __naked static void mg_ret1(void)
{
	asm volatile (
	"r0 = 1;"
	"exit;"
	::: __clobber_all);
}

__used __naked static void mg_ret2(void)
{
	asm volatile (
	"r0 = 2;"
	"exit;"
	::: __clobber_all);
}

__used __naked static void mg_call_first(void)
{
	asm volatile (
	"call mg_ret1;"
	"r1 = 0;"
"1:"
	".8byte %[may_goto];"
	"r1 += 1;"
	"if r1 < 10 goto 1b;"
	"exit;"
	:
	: __imm_insn(may_goto, BPF_RAW_INSN(BPF_JMP | BPF_JCOND, 0, 0, 2, 0))
	: __clobber_all);
}

/*
 * mg_ret1 and mg_ret2 are placed in front of mg_call_first,
 * so the offset of its call is negative.
 */
SEC("socket")
__description("may_goto in subprog that starts with a call")
__success
__retval(1)
__naked void may_goto_subprog_call_first(void)
{
	asm volatile (
	"call mg_ret1;"
	"call mg_ret2;"
	"call mg_call_first;"
	"exit;"
	::: __clobber_all);
}

__used __naked static void mg_cb_stop(void)
{
	asm volatile (
	"r0 = 1;"
	"exit;"
	::: __clobber_all);
}

__used __naked static void mg_cb_cont(void)
{
	asm volatile (
	"r0 = 0;"
	"exit;"
	::: __clobber_all);
}

/* r1 is zero, but not a constant, so that bpf_loop() is not inlined */
__used __naked static void mg_func_first(void)
{
	asm volatile (
	"r2 = %[mg_cb_stop] ll;"
	"r4 = r1;"
	"r1 = 2;"
	"r3 = 0;"
	"call %[bpf_loop];"
	"r1 = 0;"
"1:"
	".8byte %[may_goto];"
	"r1 += 1;"
	"if r1 < 10 goto 1b;"
	"exit;"
	:
	: __imm_addr(mg_cb_stop),
	  __imm(bpf_loop),
	  __imm_insn(may_goto, BPF_RAW_INSN(BPF_JMP | BPF_JCOND, 0, 0, 2, 0))
	: __clobber_all);
}

/*
 * mg_cb_stop stops bpf_loop() after the first iteration,
 * mg_cb_cont lets it do both.
 */
SEC("socket")
__description("may_goto in subprog that starts with ld_imm64 of a func")
__success
__retval(1)
__naked void may_goto_subprog_func_first(void)
{
	asm volatile (
	"r1 = 1;"
	"r2 = %[mg_cb_stop] ll;"
	"r3 = 0;"
	"r4 = 0;"
	"call %[bpf_loop];"
	"r1 = 1;"
	"r2 = %[mg_cb_cont] ll;"
	"r3 = 0;"
	"r4 = 0;"
	"call %[bpf_loop];"
	"call %[bpf_ktime_get_ns];"
	"r1 = r0;"
	"r1 >>= 63;"
	"call mg_func_first;"
	"exit;"
	:
	: __imm_addr(mg_cb_stop),
	  __imm_addr(mg_cb_cont),
	  __imm(bpf_loop),
	  __imm(bpf_ktime_get_ns)
	: __clobber_all);
}

char _license[] SEC("license") = "GPL";
