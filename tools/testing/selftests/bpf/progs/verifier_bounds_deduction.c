// SPDX-License-Identifier: GPL-2.0
/* Converted from tools/testing/selftests/bpf/verifier/bounds_deduction.c */

#include <linux/bpf.h>
#include <bpf/bpf_helpers.h>
#include "bpf_misc.h"

SEC("socket")
__description("check deducing bounds from const, 1")
__failure __msg("R0 tried to subtract pointer from scalar")
__msg_unpriv("R1 has pointer with unsupported alu operation")
__naked void deducing_bounds_from_const_1(void)
{
	asm volatile ("					\
	r0 = 1;						\
	if r0 s>= 1 goto l0_%=;				\
l0_%=:	r0 -= r1;					\
	exit;						\
"	::: __clobber_all);
}

SEC("socket")
__description("check deducing bounds from const, 2")
__success __failure_unpriv
__msg_unpriv("R1 has pointer with unsupported alu operation")
__retval(1)
__naked void deducing_bounds_from_const_2(void)
{
	asm volatile ("					\
	r0 = 1;						\
	if r0 s>= 1 goto l0_%=;				\
	exit;						\
l0_%=:	if r0 s<= 1 goto l1_%=;				\
	exit;						\
l1_%=:	r1 -= r0;					\
	exit;						\
"	::: __clobber_all);
}

SEC("socket")
__description("check deducing bounds from const, 3")
__failure __msg("R0 tried to subtract pointer from scalar")
__msg_unpriv("R1 has pointer with unsupported alu operation")
__naked void deducing_bounds_from_const_3(void)
{
	asm volatile ("					\
	r0 = 0;						\
	if r0 s<= 0 goto l0_%=;				\
l0_%=:	r0 -= r1;					\
	exit;						\
"	::: __clobber_all);
}

SEC("socket")
__description("check deducing bounds from const, 4")
__success __failure_unpriv
__msg_unpriv("R6 has pointer with unsupported alu operation")
__retval(0)
__naked void deducing_bounds_from_const_4(void)
{
	asm volatile ("					\
	r6 = r1;					\
	r0 = 0;						\
	if r0 s<= 0 goto l0_%=;				\
	exit;						\
l0_%=:	if r0 s>= 0 goto l1_%=;				\
	exit;						\
l1_%=:	r6 -= r0;					\
	exit;						\
"	::: __clobber_all);
}

SEC("socket")
__description("check deducing bounds from const, 5")
__failure __msg("R0 tried to subtract pointer from scalar")
__msg_unpriv("R1 has pointer with unsupported alu operation")
__naked void deducing_bounds_from_const_5(void)
{
	asm volatile ("					\
	r0 = 0;						\
	if r0 s>= 1 goto l0_%=;				\
	r0 -= r1;					\
l0_%=:	exit;						\
"	::: __clobber_all);
}

SEC("socket")
__description("check deducing bounds from const, 6")
__failure __msg("R0 tried to subtract pointer from scalar")
__msg_unpriv("R1 has pointer with unsupported alu operation")
__naked void deducing_bounds_from_const_6(void)
{
	asm volatile ("					\
	r0 = 0;						\
	if r0 s>= 0 goto l0_%=;				\
	exit;						\
l0_%=:	r0 -= r1;					\
	exit;						\
"	::: __clobber_all);
}

SEC("socket")
__description("check deducing bounds from const, 7")
__failure __msg("dereference of modified ctx ptr")
__msg_unpriv("R1 has pointer with unsupported alu operation")
__flag(BPF_F_ANY_ALIGNMENT)
__naked void deducing_bounds_from_const_7(void)
{
	asm volatile ("					\
	r0 = %[__imm_0];				\
	if r0 s>= 0 goto l0_%=;				\
l0_%=:	r1 -= r0;					\
	r0 = *(u32*)(r1 + %[__sk_buff_mark]);		\
	exit;						\
"	:
	: __imm_const(__imm_0, ~0),
	  __imm_const(__sk_buff_mark, offsetof(struct __sk_buff, mark))
	: __clobber_all);
}

SEC("socket")
__description("check deducing bounds from const, 8")
__failure __msg("negative offset ctx ptr R1 off=-1 disallowed")
__msg_unpriv("R1 has pointer with unsupported alu operation")
__flag(BPF_F_ANY_ALIGNMENT)
__naked void deducing_bounds_from_const_8(void)
{
	asm volatile ("					\
	r0 = %[__imm_0];				\
	if r0 s>= 0 goto l0_%=;				\
	r1 += r0;					\
l0_%=:	r0 = *(u32*)(r1 + %[__sk_buff_mark]);		\
	exit;						\
"	:
	: __imm_const(__imm_0, ~0),
	  __imm_const(__sk_buff_mark, offsetof(struct __sk_buff, mark))
	: __clobber_all);
}

SEC("socket")
__description("check deducing bounds from const, 9")
__failure __msg("R0 tried to subtract pointer from scalar")
__msg_unpriv("R1 has pointer with unsupported alu operation")
__naked void deducing_bounds_from_const_9(void)
{
	asm volatile ("					\
	r0 = 0;						\
	if r0 s>= 0 goto l0_%=;				\
l0_%=:	r0 -= r1;					\
	exit;						\
"	::: __clobber_all);
}

SEC("socket")
__description("check deducing bounds from const, 10")
__failure
__msg("math between ctx pointer and register with unbounded min value is not allowed")
__failure_unpriv
__naked void deducing_bounds_from_const_10(void)
{
	asm volatile ("					\
	r0 = 0;						\
	if r0 s<= 0 goto l0_%=;				\
l0_%=:	/* Marks reg as unknown. */			\
	r0 = -r0;					\
	r0 -= r1;					\
	exit;						\
"	::: __clobber_all);
}

char _license[] SEC("license") = "GPL";
