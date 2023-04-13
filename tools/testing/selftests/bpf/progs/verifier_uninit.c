// SPDX-License-Identifier: GPL-2.0
/* Converted from tools/testing/selftests/bpf/verifier/uninit.c */

#include <linux/bpf.h>
#include <bpf/bpf_helpers.h>
#include "../../../include/linux/filter.h"
#include "bpf_misc.h"

SEC("socket")
__description("read uninitialized register")
__failure __msg("R2 !read_ok")
__failure_unpriv
__naked void read_uninitialized_register(void)
{
	asm volatile ("					\
	r0 = r2;					\
	exit;						\
"	::: __clobber_all);
}

SEC("socket")
__description("read invalid register")
__failure __msg("R15 is invalid")
__failure_unpriv
__naked void read_invalid_register(void)
{
	asm volatile ("					\
	.8byte %[mov64_reg];				\
	exit;						\
"	:
	: __imm_insn(mov64_reg, BPF_MOV64_REG(BPF_REG_0, -1))
	: __clobber_all);
}

SEC("socket")
__description("program doesn't init R0 before exit")
__failure __msg("R0 !read_ok")
__failure_unpriv
__naked void t_init_r0_before_exit(void)
{
	asm volatile ("					\
	r2 = r1;					\
	exit;						\
"	::: __clobber_all);
}

SEC("socket")
__description("program doesn't init R0 before exit in all branches")
__failure __msg("R0 !read_ok")
__msg_unpriv("R1 pointer comparison")
__naked void before_exit_in_all_branches(void)
{
	asm volatile ("					\
	if r1 >= 0 goto l0_%=;				\
	r0 = 1;						\
	r0 += 2;					\
l0_%=:	exit;						\
"	::: __clobber_all);
}

char _license[] SEC("license") = "GPL";
