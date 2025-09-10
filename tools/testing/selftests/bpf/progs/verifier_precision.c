// SPDX-License-Identifier: GPL-2.0
/* Copyright (C) 2023 SUSE LLC */
#include <linux/bpf.h>
#include <bpf/bpf_helpers.h>
#include "../../../include/linux/filter.h"
#include "bpf_misc.h"

SEC("?raw_tp")
__success __log_level(2)
__msg("mark_precise: frame0: regs=r2 stack= before 3: (bf) r1 = r10")
__msg("mark_precise: frame0: regs=r2 stack= before 2: (55) if r2 != 0xfffffff8 goto pc+2")
__msg("mark_precise: frame0: regs=r2 stack= before 1: (87) r2 = -r2")
__msg("mark_precise: frame0: regs=r2 stack= before 0: (b7) r2 = 8")
__naked int bpf_neg(void)
{
	asm volatile (
		"r2 = 8;"
		"r2 = -r2;"
		"if r2 != -8 goto 1f;"
		"r1 = r10;"
		"r1 += r2;"
	"1:"
		"r0 = 0;"
		"exit;"
		::: __clobber_all);
}

SEC("?raw_tp")
__success __log_level(2)
__msg("mark_precise: frame0: regs=r2 stack= before 3: (bf) r1 = r10")
__msg("mark_precise: frame0: regs=r2 stack= before 2: (55) if r2 != 0x0 goto pc+2")
__msg("mark_precise: frame0: regs=r2 stack= before 1: (d4) r2 = le16 r2")
__msg("mark_precise: frame0: regs=r2 stack= before 0: (b7) r2 = 0")
__naked int bpf_end_to_le(void)
{
	asm volatile (
		"r2 = 0;"
		"r2 = le16 r2;"
		"if r2 != 0 goto 1f;"
		"r1 = r10;"
		"r1 += r2;"
	"1:"
		"r0 = 0;"
		"exit;"
		::: __clobber_all);
}


SEC("?raw_tp")
__success __log_level(2)
__msg("mark_precise: frame0: regs=r2 stack= before 3: (bf) r1 = r10")
__msg("mark_precise: frame0: regs=r2 stack= before 2: (55) if r2 != 0x0 goto pc+2")
__msg("mark_precise: frame0: regs=r2 stack= before 1: (dc) r2 = be16 r2")
__msg("mark_precise: frame0: regs=r2 stack= before 0: (b7) r2 = 0")
__naked int bpf_end_to_be(void)
{
	asm volatile (
		"r2 = 0;"
		"r2 = be16 r2;"
		"if r2 != 0 goto 1f;"
		"r1 = r10;"
		"r1 += r2;"
	"1:"
		"r0 = 0;"
		"exit;"
		::: __clobber_all);
}

#if (defined(__TARGET_ARCH_arm64) || defined(__TARGET_ARCH_x86) || \
	(defined(__TARGET_ARCH_riscv) && __riscv_xlen == 64) || \
	defined(__TARGET_ARCH_arm) || defined(__TARGET_ARCH_s390)) && \
	__clang_major__ >= 18

SEC("?raw_tp")
__success __log_level(2)
__msg("mark_precise: frame0: regs=r2 stack= before 3: (bf) r1 = r10")
__msg("mark_precise: frame0: regs=r2 stack= before 2: (55) if r2 != 0x0 goto pc+2")
__msg("mark_precise: frame0: regs=r2 stack= before 1: (d7) r2 = bswap16 r2")
__msg("mark_precise: frame0: regs=r2 stack= before 0: (b7) r2 = 0")
__naked int bpf_end_bswap(void)
{
	asm volatile (
		"r2 = 0;"
		"r2 = bswap16 r2;"
		"if r2 != 0 goto 1f;"
		"r1 = r10;"
		"r1 += r2;"
	"1:"
		"r0 = 0;"
		"exit;"
		::: __clobber_all);
}

#ifdef CAN_USE_LOAD_ACQ_STORE_REL

SEC("?raw_tp")
__success __log_level(2)
__msg("mark_precise: frame0: regs=r2 stack= before 3: (bf) r3 = r10")
__msg("mark_precise: frame0: regs=r2 stack= before 2: (db) r2 = load_acquire((u64 *)(r10 -8))")
__msg("mark_precise: frame0: regs= stack=-8 before 1: (7b) *(u64 *)(r10 -8) = r1")
__msg("mark_precise: frame0: regs=r1 stack= before 0: (b7) r1 = 8")
__naked int bpf_load_acquire(void)
{
	asm volatile (
	"r1 = 8;"
	"*(u64 *)(r10 - 8) = r1;"
	".8byte %[load_acquire_insn];" /* r2 = load_acquire((u64 *)(r10 - 8)); */
	"r3 = r10;"
	"r3 += r2;" /* mark_precise */
	"r0 = 0;"
	"exit;"
	:
	: __imm_insn(load_acquire_insn,
		     BPF_ATOMIC_OP(BPF_DW, BPF_LOAD_ACQ, BPF_REG_2, BPF_REG_10, -8))
	: __clobber_all);
}

SEC("?raw_tp")
__success __log_level(2)
__msg("mark_precise: frame0: regs=r1 stack= before 3: (bf) r2 = r10")
__msg("mark_precise: frame0: regs=r1 stack= before 2: (79) r1 = *(u64 *)(r10 -8)")
__msg("mark_precise: frame0: regs= stack=-8 before 1: (db) store_release((u64 *)(r10 -8), r1)")
__msg("mark_precise: frame0: regs=r1 stack= before 0: (b7) r1 = 8")
__naked int bpf_store_release(void)
{
	asm volatile (
	"r1 = 8;"
	".8byte %[store_release_insn];" /* store_release((u64 *)(r10 - 8), r1); */
	"r1 = *(u64 *)(r10 - 8);"
	"r2 = r10;"
	"r2 += r1;" /* mark_precise */
	"r0 = 0;"
	"exit;"
	:
	: __imm_insn(store_release_insn,
		     BPF_ATOMIC_OP(BPF_DW, BPF_STORE_REL, BPF_REG_10, BPF_REG_1, -8))
	: __clobber_all);
}

#endif /* CAN_USE_LOAD_ACQ_STORE_REL */
#endif /* v4 instruction */

SEC("?raw_tp")
__success __log_level(2)
/*
 * Without the bug fix there will be no history between "last_idx 3 first_idx 3"
 * and "parent state regs=" lines. "R0_w=6" parts are here to help anchor
 * expected log messages to the one specific mark_chain_precision operation.
 *
 * This is quite fragile: if verifier checkpointing heuristic changes, this
 * might need adjusting.
 */
__msg("2: (07) r0 += 1                       ; R0_w=6")
__msg("3: (35) if r0 >= 0xa goto pc+1")
__msg("mark_precise: frame0: last_idx 3 first_idx 3 subseq_idx -1")
__msg("mark_precise: frame0: regs=r0 stack= before 2: (07) r0 += 1")
__msg("mark_precise: frame0: regs=r0 stack= before 1: (07) r0 += 1")
__msg("mark_precise: frame0: regs=r0 stack= before 4: (05) goto pc-4")
__msg("mark_precise: frame0: regs=r0 stack= before 3: (35) if r0 >= 0xa goto pc+1")
__msg("mark_precise: frame0: parent state regs= stack=:  R0_rw=P4")
__msg("3: R0_w=6")
__naked int state_loop_first_last_equal(void)
{
	asm volatile (
		"r0 = 0;"
	"l0_%=:"
		"r0 += 1;"
		"r0 += 1;"
		/* every few iterations we'll have a checkpoint here with
		 * first_idx == last_idx, potentially confusing precision
		 * backtracking logic
		 */
		"if r0 >= 10 goto l1_%=;"	/* checkpoint + mark_precise */
		"goto l0_%=;"
	"l1_%=:"
		"exit;"
		::: __clobber_common
	);
}

__used __naked static void __bpf_cond_op_r10(void)
{
	asm volatile (
	"r2 = 2314885393468386424 ll;"
	"goto +0;"
	"if r2 <= r10 goto +3;"
	"if r1 >= -1835016 goto +0;"
	"if r2 <= 8 goto +0;"
	"if r3 <= 0 goto +0;"
	"exit;"
	::: __clobber_all);
}

SEC("?raw_tp")
__success __log_level(2)
__msg("8: (bd) if r2 <= r10 goto pc+3")
__msg("9: (35) if r1 >= 0xffe3fff8 goto pc+0")
__msg("10: (b5) if r2 <= 0x8 goto pc+0")
__msg("mark_precise: frame1: last_idx 10 first_idx 0 subseq_idx -1")
__msg("mark_precise: frame1: regs=r2 stack= before 9: (35) if r1 >= 0xffe3fff8 goto pc+0")
__msg("mark_precise: frame1: regs=r2 stack= before 8: (bd) if r2 <= r10 goto pc+3")
__msg("mark_precise: frame1: regs=r2 stack= before 7: (05) goto pc+0")
__naked void bpf_cond_op_r10(void)
{
	asm volatile (
	"r3 = 0 ll;"
	"call __bpf_cond_op_r10;"
	"r0 = 0;"
	"exit;"
	::: __clobber_all);
}

SEC("?raw_tp")
__success __log_level(2)
__msg("3: (bf) r3 = r10")
__msg("4: (bd) if r3 <= r2 goto pc+1")
__msg("5: (b5) if r2 <= 0x8 goto pc+2")
__msg("mark_precise: frame0: last_idx 5 first_idx 0 subseq_idx -1")
__msg("mark_precise: frame0: regs=r2 stack= before 4: (bd) if r3 <= r2 goto pc+1")
__msg("mark_precise: frame0: regs=r2 stack= before 3: (bf) r3 = r10")
__naked void bpf_cond_op_not_r10(void)
{
	asm volatile (
	"r0 = 0;"
	"r2 = 2314885393468386424 ll;"
	"r3 = r10;"
	"if r3 <= r2 goto +1;"
	"if r2 <= 8 goto +2;"
	"r0 = 2 ll;"
	"exit;"
	::: __clobber_all);
}

SEC("lsm.s/socket_connect")
__success __log_level(2)
__msg("0: (b7) r0 = 1                        ; R0_w=1")
__msg("1: (84) w0 = -w0                      ; R0_w=0xffffffff")
__msg("mark_precise: frame0: last_idx 2 first_idx 0 subseq_idx -1")
__msg("mark_precise: frame0: regs=r0 stack= before 1: (84) w0 = -w0")
__msg("mark_precise: frame0: regs=r0 stack= before 0: (b7) r0 = 1")
__naked int bpf_neg_2(void)
{
	/*
	 * lsm.s/socket_connect requires a return value within [-4095, 0].
	 * Returning -1 is allowed
	 */
	asm volatile (
	"r0 = 1;"
	"w0 = -w0;"
	"exit;"
	::: __clobber_all);
}

SEC("lsm.s/socket_connect")
__failure __msg("At program exit the register R0 has")
__naked int bpf_neg_3(void)
{
	/*
	 * lsm.s/socket_connect requires a return value within [-4095, 0].
	 * Returning -10000 is not allowed.
	 */
	asm volatile (
	"r0 = 10000;"
	"w0 = -w0;"
	"exit;"
	::: __clobber_all);
}

SEC("lsm.s/socket_connect")
__success __log_level(2)
__msg("0: (b7) r0 = 1                        ; R0_w=1")
__msg("1: (87) r0 = -r0                      ; R0_w=-1")
__msg("mark_precise: frame0: last_idx 2 first_idx 0 subseq_idx -1")
__msg("mark_precise: frame0: regs=r0 stack= before 1: (87) r0 = -r0")
__msg("mark_precise: frame0: regs=r0 stack= before 0: (b7) r0 = 1")
__naked int bpf_neg_4(void)
{
	/*
	 * lsm.s/socket_connect requires a return value within [-4095, 0].
	 * Returning -1 is allowed
	 */
	asm volatile (
	"r0 = 1;"
	"r0 = -r0;"
	"exit;"
	::: __clobber_all);
}

SEC("lsm.s/socket_connect")
__failure __msg("At program exit the register R0 has")
__naked int bpf_neg_5(void)
{
	/*
	 * lsm.s/socket_connect requires a return value within [-4095, 0].
	 * Returning -10000 is not allowed.
	 */
	asm volatile (
	"r0 = 10000;"
	"r0 = -r0;"
	"exit;"
	::: __clobber_all);
}

char _license[] SEC("license") = "GPL";
