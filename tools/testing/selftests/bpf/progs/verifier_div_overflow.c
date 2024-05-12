// SPDX-License-Identifier: GPL-2.0
/* Converted from tools/testing/selftests/bpf/verifier/div_overflow.c */

#include <linux/bpf.h>
#include <bpf/bpf_helpers.h>
#include <limits.h>
#include "bpf_misc.h"

/* Just make sure that JITs used udiv/umod as otherwise we get
 * an exception from INT_MIN/-1 overflow similarly as with div
 * by zero.
 */

SEC("tc")
__description("DIV32 overflow, check 1")
__success __retval(0)
__naked void div32_overflow_check_1(void)
{
	asm volatile ("					\
	w1 = -1;					\
	w0 = %[int_min];				\
	w0 /= w1;					\
	exit;						\
"	:
	: __imm_const(int_min, INT_MIN)
	: __clobber_all);
}

SEC("tc")
__description("DIV32 overflow, check 2")
__success __retval(0)
__naked void div32_overflow_check_2(void)
{
	asm volatile ("					\
	w0 = %[int_min];				\
	w0 /= -1;					\
	exit;						\
"	:
	: __imm_const(int_min, INT_MIN)
	: __clobber_all);
}

SEC("tc")
__description("DIV64 overflow, check 1")
__success __retval(0)
__naked void div64_overflow_check_1(void)
{
	asm volatile ("					\
	r1 = -1;					\
	r2 = %[llong_min] ll;				\
	r2 /= r1;					\
	w0 = 0;						\
	if r0 == r2 goto l0_%=;				\
	w0 = 1;						\
l0_%=:	exit;						\
"	:
	: __imm_const(llong_min, LLONG_MIN)
	: __clobber_all);
}

SEC("tc")
__description("DIV64 overflow, check 2")
__success __retval(0)
__naked void div64_overflow_check_2(void)
{
	asm volatile ("					\
	r1 = %[llong_min] ll;				\
	r1 /= -1;					\
	w0 = 0;						\
	if r0 == r1 goto l0_%=;				\
	w0 = 1;						\
l0_%=:	exit;						\
"	:
	: __imm_const(llong_min, LLONG_MIN)
	: __clobber_all);
}

SEC("tc")
__description("MOD32 overflow, check 1")
__success __retval(INT_MIN)
__naked void mod32_overflow_check_1(void)
{
	asm volatile ("					\
	w1 = -1;					\
	w0 = %[int_min];				\
	w0 %%= w1;					\
	exit;						\
"	:
	: __imm_const(int_min, INT_MIN)
	: __clobber_all);
}

SEC("tc")
__description("MOD32 overflow, check 2")
__success __retval(INT_MIN)
__naked void mod32_overflow_check_2(void)
{
	asm volatile ("					\
	w0 = %[int_min];				\
	w0 %%= -1;					\
	exit;						\
"	:
	: __imm_const(int_min, INT_MIN)
	: __clobber_all);
}

SEC("tc")
__description("MOD64 overflow, check 1")
__success __retval(1)
__naked void mod64_overflow_check_1(void)
{
	asm volatile ("					\
	r1 = -1;					\
	r2 = %[llong_min] ll;				\
	r3 = r2;					\
	r2 %%= r1;					\
	w0 = 0;						\
	if r3 != r2 goto l0_%=;				\
	w0 = 1;						\
l0_%=:	exit;						\
"	:
	: __imm_const(llong_min, LLONG_MIN)
	: __clobber_all);
}

SEC("tc")
__description("MOD64 overflow, check 2")
__success __retval(1)
__naked void mod64_overflow_check_2(void)
{
	asm volatile ("					\
	r2 = %[llong_min] ll;				\
	r3 = r2;					\
	r2 %%= -1;					\
	w0 = 0;						\
	if r3 != r2 goto l0_%=;				\
	w0 = 1;						\
l0_%=:	exit;						\
"	:
	: __imm_const(llong_min, LLONG_MIN)
	: __clobber_all);
}

char _license[] SEC("license") = "GPL";
