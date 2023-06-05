// SPDX-License-Identifier: GPL-2.0
/* Converted from tools/testing/selftests/bpf/verifier/div0.c */

#include <linux/bpf.h>
#include <bpf/bpf_helpers.h>
#include "bpf_misc.h"

SEC("socket")
__description("DIV32 by 0, zero check 1")
__success __success_unpriv __retval(42)
__naked void by_0_zero_check_1_1(void)
{
	asm volatile ("					\
	w0 = 42;					\
	w1 = 0;						\
	w2 = 1;						\
	w2 /= w1;					\
	exit;						\
"	::: __clobber_all);
}

SEC("socket")
__description("DIV32 by 0, zero check 2")
__success __success_unpriv __retval(42)
__naked void by_0_zero_check_2_1(void)
{
	asm volatile ("					\
	w0 = 42;					\
	r1 = 0xffffffff00000000LL ll;			\
	w2 = 1;						\
	w2 /= w1;					\
	exit;						\
"	::: __clobber_all);
}

SEC("socket")
__description("DIV64 by 0, zero check")
__success __success_unpriv __retval(42)
__naked void div64_by_0_zero_check(void)
{
	asm volatile ("					\
	w0 = 42;					\
	w1 = 0;						\
	w2 = 1;						\
	r2 /= r1;					\
	exit;						\
"	::: __clobber_all);
}

SEC("socket")
__description("MOD32 by 0, zero check 1")
__success __success_unpriv __retval(42)
__naked void by_0_zero_check_1_2(void)
{
	asm volatile ("					\
	w0 = 42;					\
	w1 = 0;						\
	w2 = 1;						\
	w2 %%= w1;					\
	exit;						\
"	::: __clobber_all);
}

SEC("socket")
__description("MOD32 by 0, zero check 2")
__success __success_unpriv __retval(42)
__naked void by_0_zero_check_2_2(void)
{
	asm volatile ("					\
	w0 = 42;					\
	r1 = 0xffffffff00000000LL ll;			\
	w2 = 1;						\
	w2 %%= w1;					\
	exit;						\
"	::: __clobber_all);
}

SEC("socket")
__description("MOD64 by 0, zero check")
__success __success_unpriv __retval(42)
__naked void mod64_by_0_zero_check(void)
{
	asm volatile ("					\
	w0 = 42;					\
	w1 = 0;						\
	w2 = 1;						\
	r2 %%= r1;					\
	exit;						\
"	::: __clobber_all);
}

SEC("tc")
__description("DIV32 by 0, zero check ok, cls")
__success __retval(8)
__naked void _0_zero_check_ok_cls_1(void)
{
	asm volatile ("					\
	w0 = 42;					\
	w1 = 2;						\
	w2 = 16;					\
	w2 /= w1;					\
	r0 = r2;					\
	exit;						\
"	::: __clobber_all);
}

SEC("tc")
__description("DIV32 by 0, zero check 1, cls")
__success __retval(0)
__naked void _0_zero_check_1_cls_1(void)
{
	asm volatile ("					\
	w1 = 0;						\
	w0 = 1;						\
	w0 /= w1;					\
	exit;						\
"	::: __clobber_all);
}

SEC("tc")
__description("DIV32 by 0, zero check 2, cls")
__success __retval(0)
__naked void _0_zero_check_2_cls_1(void)
{
	asm volatile ("					\
	r1 = 0xffffffff00000000LL ll;			\
	w0 = 1;						\
	w0 /= w1;					\
	exit;						\
"	::: __clobber_all);
}

SEC("tc")
__description("DIV64 by 0, zero check, cls")
__success __retval(0)
__naked void by_0_zero_check_cls(void)
{
	asm volatile ("					\
	w1 = 0;						\
	w0 = 1;						\
	r0 /= r1;					\
	exit;						\
"	::: __clobber_all);
}

SEC("tc")
__description("MOD32 by 0, zero check ok, cls")
__success __retval(2)
__naked void _0_zero_check_ok_cls_2(void)
{
	asm volatile ("					\
	w0 = 42;					\
	w1 = 3;						\
	w2 = 5;						\
	w2 %%= w1;					\
	r0 = r2;					\
	exit;						\
"	::: __clobber_all);
}

SEC("tc")
__description("MOD32 by 0, zero check 1, cls")
__success __retval(1)
__naked void _0_zero_check_1_cls_2(void)
{
	asm volatile ("					\
	w1 = 0;						\
	w0 = 1;						\
	w0 %%= w1;					\
	exit;						\
"	::: __clobber_all);
}

SEC("tc")
__description("MOD32 by 0, zero check 2, cls")
__success __retval(1)
__naked void _0_zero_check_2_cls_2(void)
{
	asm volatile ("					\
	r1 = 0xffffffff00000000LL ll;			\
	w0 = 1;						\
	w0 %%= w1;					\
	exit;						\
"	::: __clobber_all);
}

SEC("tc")
__description("MOD64 by 0, zero check 1, cls")
__success __retval(2)
__naked void _0_zero_check_1_cls_3(void)
{
	asm volatile ("					\
	w1 = 0;						\
	w0 = 2;						\
	r0 %%= r1;					\
	exit;						\
"	::: __clobber_all);
}

SEC("tc")
__description("MOD64 by 0, zero check 2, cls")
__success __retval(-1)
__naked void _0_zero_check_2_cls_3(void)
{
	asm volatile ("					\
	w1 = 0;						\
	w0 = -1;					\
	r0 %%= r1;					\
	exit;						\
"	::: __clobber_all);
}

char _license[] SEC("license") = "GPL";
