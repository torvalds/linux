// SPDX-License-Identifier: GPL-2.0
/* Converted from tools/testing/selftests/bpf/verifier/ld_ind.c */

#include <linux/bpf.h>
#include <bpf/bpf_helpers.h>
#include "../../../include/linux/filter.h"
#include "bpf_misc.h"

SEC("socket")
__description("ld_ind: check calling conv, r1")
__failure __msg("R1 !read_ok")
__failure_unpriv
__naked void ind_check_calling_conv_r1(void)
{
	asm volatile ("					\
	r6 = r1;					\
	r1 = 1;						\
	.8byte %[ld_ind];				\
	r0 = r1;					\
	exit;						\
"	:
	: __imm_insn(ld_ind, BPF_LD_IND(BPF_W, BPF_REG_1, -0x200000))
	: __clobber_all);
}

SEC("socket")
__description("ld_ind: check calling conv, r2")
__failure __msg("R2 !read_ok")
__failure_unpriv
__naked void ind_check_calling_conv_r2(void)
{
	asm volatile ("					\
	r6 = r1;					\
	r2 = 1;						\
	.8byte %[ld_ind];				\
	r0 = r2;					\
	exit;						\
"	:
	: __imm_insn(ld_ind, BPF_LD_IND(BPF_W, BPF_REG_2, -0x200000))
	: __clobber_all);
}

SEC("socket")
__description("ld_ind: check calling conv, r3")
__failure __msg("R3 !read_ok")
__failure_unpriv
__naked void ind_check_calling_conv_r3(void)
{
	asm volatile ("					\
	r6 = r1;					\
	r3 = 1;						\
	.8byte %[ld_ind];				\
	r0 = r3;					\
	exit;						\
"	:
	: __imm_insn(ld_ind, BPF_LD_IND(BPF_W, BPF_REG_3, -0x200000))
	: __clobber_all);
}

SEC("socket")
__description("ld_ind: check calling conv, r4")
__failure __msg("R4 !read_ok")
__failure_unpriv
__naked void ind_check_calling_conv_r4(void)
{
	asm volatile ("					\
	r6 = r1;					\
	r4 = 1;						\
	.8byte %[ld_ind];				\
	r0 = r4;					\
	exit;						\
"	:
	: __imm_insn(ld_ind, BPF_LD_IND(BPF_W, BPF_REG_4, -0x200000))
	: __clobber_all);
}

SEC("socket")
__description("ld_ind: check calling conv, r5")
__failure __msg("R5 !read_ok")
__failure_unpriv
__naked void ind_check_calling_conv_r5(void)
{
	asm volatile ("					\
	r6 = r1;					\
	r5 = 1;						\
	.8byte %[ld_ind];				\
	r0 = r5;					\
	exit;						\
"	:
	: __imm_insn(ld_ind, BPF_LD_IND(BPF_W, BPF_REG_5, -0x200000))
	: __clobber_all);
}

SEC("socket")
__description("ld_ind: check calling conv, r7")
__success __success_unpriv __retval(1)
__naked void ind_check_calling_conv_r7(void)
{
	asm volatile ("					\
	r6 = r1;					\
	r7 = 1;						\
	.8byte %[ld_ind];				\
	r0 = r7;					\
	exit;						\
"	:
	: __imm_insn(ld_ind, BPF_LD_IND(BPF_W, BPF_REG_7, -0x200000))
	: __clobber_all);
}

char _license[] SEC("license") = "GPL";
