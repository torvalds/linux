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
__xlated("0: *(u64 *)(r10 -16) = 65535")
__xlated("1: *(u64 *)(r10 -8) = 0")
__xlated("2: r11 = *(u64 *)(r10 -16)")
__xlated("3: if r11 == 0x0 goto pc+6")
__xlated("4: r11 -= 1")
__xlated("5: if r11 != 0x0 goto pc+2")
__xlated("6: r11 = -16")
__xlated("7: call unknown")
__xlated("8: *(u64 *)(r10 -16) = r11")
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

char _license[] SEC("license") = "GPL";
