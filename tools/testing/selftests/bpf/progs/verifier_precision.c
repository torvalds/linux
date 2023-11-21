// SPDX-License-Identifier: GPL-2.0
/* Copyright (C) 2023 SUSE LLC */
#include <linux/bpf.h>
#include <bpf/bpf_helpers.h>
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

#endif /* v4 instruction */
