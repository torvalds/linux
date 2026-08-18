// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2026 Meta Platforms, Inc. and affiliates. */

#include <vmlinux.h>
#include <bpf/bpf_helpers.h>
#include "../test_kmods/bpf_testmod_kfunc.h"
#include "bpf_misc.h"

#if (defined(__TARGET_ARCH_x86) || defined(__TARGET_ARCH_arm64)) && \
	defined(__BPF_FEATURE_STACK_ARGUMENT)

/* Force kfunc extern BTF generation for inline asm call below.
 * Uses its own SEC so it's not included as a .text subprog.
 * The '?' prefix sets autoload=false so libbpf won't load it.
 */
SEC("?tc")
int __btf_kfunc_gen(struct __sk_buff *ctx)
{
	char buf[8] = {};

	return bpf_kfunc_call_stack_arg_mem(0, 0, 0, 0, 0, buf, sizeof(buf));
}

/*
 * Test precision backtracking across bpf-to-bpf call for kfunc stack arg.
 * subprog_call_mem_kfunc receives a size as incoming stack arg (arg6)
 * and forwards it as mem__sz (arg7) to bpf_kfunc_call_stack_arg_mem.
 */
__naked __noinline __used
static long subprog_call_mem_kfunc(long a, long b, long c, long d, long e, long size)
{
	asm volatile (
		"r1 = *(u64 *)(r11 + 8);"	/* r1 = incoming arg6 (size) */
		"r2 = 0x0807060504030201 ll;"	/* r2 = buf contents */
		"*(u64 *)(r10 - 8) = r2;"	/* store buf to stack */
		"r2 = r10;"
		"r2 += -8;"			/* r2 = &buf */
		"*(u64 *)(r11 - 8) = r2;"	/* outgoing arg6 = buf */
		"*(u64 *)(r11 - 16) = r1;"	/* outgoing arg7 = size */
		"r1 = 1;"
		"r2 = 2;"
		"r3 = 3;"
		"r4 = 4;"
		"r5 = 5;"
		"call %[bpf_kfunc_call_stack_arg_mem];"
		"exit;"
		:
		: __imm(bpf_kfunc_call_stack_arg_mem)
		: __clobber_all
	);
}

SEC("tc")
__description("stack_arg: precision backtracking across bpf2bpf call for kfunc")
__success
__log_level(2)
__flag(BPF_F_TEST_STATE_FREQ)
__btf_func_path("btf__stack_arg_precision.bpf.o")
__msg("mark_precise: frame1: last_idx 26 first_idx 13 subseq_idx -1")
__msg("mark_precise: frame1: regs= stack= before 25: (b7) r5 = 5")
__msg("mark_precise: frame1: regs= stack= before 24: (b7) r4 = 4")
__msg("mark_precise: frame1: regs= stack= before 23: (b7) r3 = 3")
__msg("mark_precise: frame1: regs= stack= before 22: (b7) r2 = 2")
__msg("mark_precise: frame1: regs= stack= before 21: (b7) r1 = 1")
__msg("mark_precise: frame1: regs= stack= before 20: (7b) *(u64 *)(r11 -16) = r1")
__msg("mark_precise: frame1: regs=r1 stack= before 19: (7b) *(u64 *)(r11 -8) = r2")
__msg("mark_precise: frame1: regs=r1 stack= before 18: (07) r2 += -8")
__msg("mark_precise: frame1: regs=r1 stack= before 17: (bf) r2 = r10")
__msg("mark_precise: frame1: regs=r1 stack= before 16: (7b) *(u64 *)(r10 -8) = r2")
__msg("mark_precise: frame1: regs=r1 stack= before 14: (18) r2 = 0x807060504030201")
__msg("mark_precise: frame1: regs=r1 stack= before 13: (79) r1 = *(u64 *)(r11 +8)")
__msg("mark_precise: frame1: parent state regs= stack=:  frame1: R10=fp0")
__msg("mark_precise: frame0: parent state regs= stack=:  R10=fp0")
__msg("mark_precise: frame1: last_idx 11 first_idx 11 subseq_idx 13")
__msg("mark_precise: frame1: regs= stack= before 11: (85) call pc+1")
__msg("mark_precise: frame0: parent state regs= stack=:  R1=1 R2=2 R3=3 R4=4 R5=5 R10=fp0")
__msg("mark_precise: frame0: last_idx 9 first_idx 7 subseq_idx 11")
__msg("mark_precise: frame0: regs= stack= before 9: (05) goto pc+1")
__msg("mark_precise: frame0: regs= stack= before 8: (7a) *(u64 *)(r11 -8) = 4")
__msg("mark_precise: frame1: last_idx 26 first_idx 13 subseq_idx -1 ")
__msg("mark_precise: frame1: regs= stack= before 25: (b7) r5 = 5")
__msg("mark_precise: frame1: regs= stack= before 24: (b7) r4 = 4")
__msg("mark_precise: frame1: regs= stack= before 23: (b7) r3 = 3")
__msg("mark_precise: frame1: regs= stack= before 22: (b7) r2 = 2")
__msg("mark_precise: frame1: regs= stack= before 21: (b7) r1 = 1")
__msg("mark_precise: frame1: regs= stack= before 20: (7b) *(u64 *)(r11 -16) = r1")
__msg("mark_precise: frame1: regs=r1 stack= before 19: (7b) *(u64 *)(r11 -8) = r2")
__msg("mark_precise: frame1: regs=r1 stack= before 18: (07) r2 += -8")
__msg("mark_precise: frame1: regs=r1 stack= before 17: (bf) r2 = r10")
__msg("mark_precise: frame1: regs=r1 stack= before 16: (7b) *(u64 *)(r10 -8) = r2")
__msg("mark_precise: frame1: regs=r1 stack= before 14: (18) r2 = 0x807060504030201")
__msg("mark_precise: frame1: regs=r1 stack= before 13: (79) r1 = *(u64 *)(r11 +8)")
__msg("mark_precise: frame1: parent state regs= stack=:  frame1: R10=fp0")
__msg("mark_precise: frame0: parent state regs= stack=:  R10=fp0")
__msg("mark_precise: frame1: last_idx 11 first_idx 11 subseq_idx 13 ")
__msg("mark_precise: frame1: regs= stack= before 11: (85) call pc+1")
__msg("mark_precise: frame0: parent state regs= stack=:  R1=1 R2=2 R3=3 R4=4 R5=5 R10=fp0")
__msg("mark_precise: frame0: last_idx 10 first_idx 10 subseq_idx 11 ")
__msg("mark_precise: frame0: regs= stack= before 10: (7a) *(u64 *)(r11 -8) = 6")
__naked void stack_arg_precision_bpf2bpf(void)
{
	asm volatile (
		"call %[bpf_get_prandom_u32];"
		"r6 = r0;"
		"r1 = 1;"
		"r2 = 2;"
		"r3 = 3;"
		"r4 = 4;"
		"r5 = 5;"
		"if r6 < 2 goto l0_%=;"
		"*(u64 *)(r11 - 8) = 4;"
		"goto l1_%=;"
	"l0_%=:"
		"*(u64 *)(r11 - 8) = 6;"
	"l1_%=:"
		"call subprog_call_mem_kfunc;"
		"exit;"
		:: __imm(bpf_get_prandom_u32)
		: __clobber_all
	);
}

#else

SEC("socket")
__description("stack_arg_precision: not supported, dummy test")
__success
int dummy_test(void)
{
	return 0;
}

#endif

char _license[] SEC("license") = "GPL";
