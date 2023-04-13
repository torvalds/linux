// SPDX-License-Identifier: GPL-2.0
/* Converted from tools/testing/selftests/bpf/verifier/masking.c */

#include <linux/bpf.h>
#include <bpf/bpf_helpers.h>
#include "bpf_misc.h"

SEC("socket")
__description("masking, test out of bounds 1")
__success __success_unpriv __retval(0)
__naked void test_out_of_bounds_1(void)
{
	asm volatile ("					\
	w1 = 5;						\
	w2 = %[__imm_0];				\
	r2 -= r1;					\
	r2 |= r1;					\
	r2 = -r2;					\
	r2 s>>= 63;					\
	r1 &= r2;					\
	r0 = r1;					\
	exit;						\
"	:
	: __imm_const(__imm_0, 5 - 1)
	: __clobber_all);
}

SEC("socket")
__description("masking, test out of bounds 2")
__success __success_unpriv __retval(0)
__naked void test_out_of_bounds_2(void)
{
	asm volatile ("					\
	w1 = 1;						\
	w2 = %[__imm_0];				\
	r2 -= r1;					\
	r2 |= r1;					\
	r2 = -r2;					\
	r2 s>>= 63;					\
	r1 &= r2;					\
	r0 = r1;					\
	exit;						\
"	:
	: __imm_const(__imm_0, 1 - 1)
	: __clobber_all);
}

SEC("socket")
__description("masking, test out of bounds 3")
__success __success_unpriv __retval(0)
__naked void test_out_of_bounds_3(void)
{
	asm volatile ("					\
	w1 = 0xffffffff;				\
	w2 = %[__imm_0];				\
	r2 -= r1;					\
	r2 |= r1;					\
	r2 = -r2;					\
	r2 s>>= 63;					\
	r1 &= r2;					\
	r0 = r1;					\
	exit;						\
"	:
	: __imm_const(__imm_0, 0xffffffff - 1)
	: __clobber_all);
}

SEC("socket")
__description("masking, test out of bounds 4")
__success __success_unpriv __retval(0)
__naked void test_out_of_bounds_4(void)
{
	asm volatile ("					\
	w1 = 0xffffffff;				\
	w2 = %[__imm_0];				\
	r2 -= r1;					\
	r2 |= r1;					\
	r2 = -r2;					\
	r2 s>>= 63;					\
	r1 &= r2;					\
	r0 = r1;					\
	exit;						\
"	:
	: __imm_const(__imm_0, 1 - 1)
	: __clobber_all);
}

SEC("socket")
__description("masking, test out of bounds 5")
__success __success_unpriv __retval(0)
__naked void test_out_of_bounds_5(void)
{
	asm volatile ("					\
	w1 = -1;					\
	w2 = %[__imm_0];				\
	r2 -= r1;					\
	r2 |= r1;					\
	r2 = -r2;					\
	r2 s>>= 63;					\
	r1 &= r2;					\
	r0 = r1;					\
	exit;						\
"	:
	: __imm_const(__imm_0, 1 - 1)
	: __clobber_all);
}

SEC("socket")
__description("masking, test out of bounds 6")
__success __success_unpriv __retval(0)
__naked void test_out_of_bounds_6(void)
{
	asm volatile ("					\
	w1 = -1;					\
	w2 = %[__imm_0];				\
	r2 -= r1;					\
	r2 |= r1;					\
	r2 = -r2;					\
	r2 s>>= 63;					\
	r1 &= r2;					\
	r0 = r1;					\
	exit;						\
"	:
	: __imm_const(__imm_0, 0xffffffff - 1)
	: __clobber_all);
}

SEC("socket")
__description("masking, test out of bounds 7")
__success __success_unpriv __retval(0)
__naked void test_out_of_bounds_7(void)
{
	asm volatile ("					\
	r1 = 5;						\
	w2 = %[__imm_0];				\
	r2 -= r1;					\
	r2 |= r1;					\
	r2 = -r2;					\
	r2 s>>= 63;					\
	r1 &= r2;					\
	r0 = r1;					\
	exit;						\
"	:
	: __imm_const(__imm_0, 5 - 1)
	: __clobber_all);
}

SEC("socket")
__description("masking, test out of bounds 8")
__success __success_unpriv __retval(0)
__naked void test_out_of_bounds_8(void)
{
	asm volatile ("					\
	r1 = 1;						\
	w2 = %[__imm_0];				\
	r2 -= r1;					\
	r2 |= r1;					\
	r2 = -r2;					\
	r2 s>>= 63;					\
	r1 &= r2;					\
	r0 = r1;					\
	exit;						\
"	:
	: __imm_const(__imm_0, 1 - 1)
	: __clobber_all);
}

SEC("socket")
__description("masking, test out of bounds 9")
__success __success_unpriv __retval(0)
__naked void test_out_of_bounds_9(void)
{
	asm volatile ("					\
	r1 = 0xffffffff;				\
	w2 = %[__imm_0];				\
	r2 -= r1;					\
	r2 |= r1;					\
	r2 = -r2;					\
	r2 s>>= 63;					\
	r1 &= r2;					\
	r0 = r1;					\
	exit;						\
"	:
	: __imm_const(__imm_0, 0xffffffff - 1)
	: __clobber_all);
}

SEC("socket")
__description("masking, test out of bounds 10")
__success __success_unpriv __retval(0)
__naked void test_out_of_bounds_10(void)
{
	asm volatile ("					\
	r1 = 0xffffffff;				\
	w2 = %[__imm_0];				\
	r2 -= r1;					\
	r2 |= r1;					\
	r2 = -r2;					\
	r2 s>>= 63;					\
	r1 &= r2;					\
	r0 = r1;					\
	exit;						\
"	:
	: __imm_const(__imm_0, 1 - 1)
	: __clobber_all);
}

SEC("socket")
__description("masking, test out of bounds 11")
__success __success_unpriv __retval(0)
__naked void test_out_of_bounds_11(void)
{
	asm volatile ("					\
	r1 = -1;					\
	w2 = %[__imm_0];				\
	r2 -= r1;					\
	r2 |= r1;					\
	r2 = -r2;					\
	r2 s>>= 63;					\
	r1 &= r2;					\
	r0 = r1;					\
	exit;						\
"	:
	: __imm_const(__imm_0, 1 - 1)
	: __clobber_all);
}

SEC("socket")
__description("masking, test out of bounds 12")
__success __success_unpriv __retval(0)
__naked void test_out_of_bounds_12(void)
{
	asm volatile ("					\
	r1 = -1;					\
	w2 = %[__imm_0];				\
	r2 -= r1;					\
	r2 |= r1;					\
	r2 = -r2;					\
	r2 s>>= 63;					\
	r1 &= r2;					\
	r0 = r1;					\
	exit;						\
"	:
	: __imm_const(__imm_0, 0xffffffff - 1)
	: __clobber_all);
}

SEC("socket")
__description("masking, test in bounds 1")
__success __success_unpriv __retval(4)
__naked void masking_test_in_bounds_1(void)
{
	asm volatile ("					\
	w1 = 4;						\
	w2 = %[__imm_0];				\
	r2 -= r1;					\
	r2 |= r1;					\
	r2 = -r2;					\
	r2 s>>= 63;					\
	r1 &= r2;					\
	r0 = r1;					\
	exit;						\
"	:
	: __imm_const(__imm_0, 5 - 1)
	: __clobber_all);
}

SEC("socket")
__description("masking, test in bounds 2")
__success __success_unpriv __retval(0)
__naked void masking_test_in_bounds_2(void)
{
	asm volatile ("					\
	w1 = 0;						\
	w2 = %[__imm_0];				\
	r2 -= r1;					\
	r2 |= r1;					\
	r2 = -r2;					\
	r2 s>>= 63;					\
	r1 &= r2;					\
	r0 = r1;					\
	exit;						\
"	:
	: __imm_const(__imm_0, 0xffffffff - 1)
	: __clobber_all);
}

SEC("socket")
__description("masking, test in bounds 3")
__success __success_unpriv __retval(0xfffffffe)
__naked void masking_test_in_bounds_3(void)
{
	asm volatile ("					\
	w1 = 0xfffffffe;				\
	w2 = %[__imm_0];				\
	r2 -= r1;					\
	r2 |= r1;					\
	r2 = -r2;					\
	r2 s>>= 63;					\
	r1 &= r2;					\
	r0 = r1;					\
	exit;						\
"	:
	: __imm_const(__imm_0, 0xffffffff - 1)
	: __clobber_all);
}

SEC("socket")
__description("masking, test in bounds 4")
__success __success_unpriv __retval(0xabcde)
__naked void masking_test_in_bounds_4(void)
{
	asm volatile ("					\
	w1 = 0xabcde;					\
	w2 = %[__imm_0];				\
	r2 -= r1;					\
	r2 |= r1;					\
	r2 = -r2;					\
	r2 s>>= 63;					\
	r1 &= r2;					\
	r0 = r1;					\
	exit;						\
"	:
	: __imm_const(__imm_0, 0xabcdef - 1)
	: __clobber_all);
}

SEC("socket")
__description("masking, test in bounds 5")
__success __success_unpriv __retval(0)
__naked void masking_test_in_bounds_5(void)
{
	asm volatile ("					\
	w1 = 0;						\
	w2 = %[__imm_0];				\
	r2 -= r1;					\
	r2 |= r1;					\
	r2 = -r2;					\
	r2 s>>= 63;					\
	r1 &= r2;					\
	r0 = r1;					\
	exit;						\
"	:
	: __imm_const(__imm_0, 1 - 1)
	: __clobber_all);
}

SEC("socket")
__description("masking, test in bounds 6")
__success __success_unpriv __retval(46)
__naked void masking_test_in_bounds_6(void)
{
	asm volatile ("					\
	w1 = 46;					\
	w2 = %[__imm_0];				\
	r2 -= r1;					\
	r2 |= r1;					\
	r2 = -r2;					\
	r2 s>>= 63;					\
	r1 &= r2;					\
	r0 = r1;					\
	exit;						\
"	:
	: __imm_const(__imm_0, 47 - 1)
	: __clobber_all);
}

SEC("socket")
__description("masking, test in bounds 7")
__success __success_unpriv __retval(46)
__naked void masking_test_in_bounds_7(void)
{
	asm volatile ("					\
	r3 = -46;					\
	r3 *= -1;					\
	w2 = %[__imm_0];				\
	r2 -= r3;					\
	r2 |= r3;					\
	r2 = -r2;					\
	r2 s>>= 63;					\
	r3 &= r2;					\
	r0 = r3;					\
	exit;						\
"	:
	: __imm_const(__imm_0, 47 - 1)
	: __clobber_all);
}

SEC("socket")
__description("masking, test in bounds 8")
__success __success_unpriv __retval(0)
__naked void masking_test_in_bounds_8(void)
{
	asm volatile ("					\
	r3 = -47;					\
	r3 *= -1;					\
	w2 = %[__imm_0];				\
	r2 -= r3;					\
	r2 |= r3;					\
	r2 = -r2;					\
	r2 s>>= 63;					\
	r3 &= r2;					\
	r0 = r3;					\
	exit;						\
"	:
	: __imm_const(__imm_0, 47 - 1)
	: __clobber_all);
}

char _license[] SEC("license") = "GPL";
